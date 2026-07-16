#include "scene/water/bore/BoreWaveProfile.h"

#include <glm/common.hpp>
#include <glm/gtc/constants.hpp>

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <vector>

namespace water
{
// 平滑插值函数
// 在 [edge0, edge1] 区间内，用 Hermite 曲线 t²(3 - 2t) 把输入 x 映射到 [0, 1] 之间的平滑值。
// 在整个剖面生成过程中，用于波峰掩码、尾部衰减等需要平滑过渡的地方，避免硬边界。
float BoreProfileSmoothStep(float edge0, float edge1, float x)
{
    float t =
        glm::clamp(
            (x - edge0) / (edge1 - edge0),
            0.0f,
            1.0f
        );

    return t * t * (3.0f - 2.0f * t);
}

// 计算以 center 为中心、width 为宽度参数的高斯函数值 exp(-((x-center)/width)²)
float BoreProfileGaussian(float x, float center, float width)
{
    float q =
        (x - center) / width;

    return std::exp(-q * q);
}

namespace
{
struct BoreProfileRowFields
{
    std::vector<float> forward;
    std::vector<float> upward;
    std::vector<float> foamSource;
    std::vector<float> crestMask;
};

// 是 GenerateStaticBoreWaveProfile 的升级版，用于生成二维剖面纹理中的一行（对应某个特定的动画相位 v）。
// 它的核心任务是：根据当前的动画相位 v，动态调整波峰的强度、宽度、位置和破碎程度，产生一整行的位移、泡沫和掩码数据。
BoreProfileRowFields GenerateProfileRow(
    const water::BoreWaveProfileConfig& config,
    float v
)
{
    BoreProfileRowFields row{};
    row.forward.resize(config.distanceResolution, 0.0f);
    row.upward.resize(config.distanceResolution, 0.0f);
    row.foamSource.resize(config.distanceResolution, 0.0f);
    row.crestMask.resize(config.distanceResolution, 0.0f);

    float formation =
        water::BoreProfileSmoothStep(
            0.0f,
            0.20f,
            v
        );

    float decay =
        1.0f -
        water::BoreProfileSmoothStep(
            0.82f,
            1.0f,
            v
        );

    float life =
        formation *
        decay;

    float breakingPhase =
        water::BoreProfileSmoothStep(
            0.35f,
            0.65f,
            v
        ) * decay;

    float animatedHeight =
        config.crestHeight *
        life;

    float animatedWidth =
        glm::mix(
            config.crestWidth * 1.3f,
            config.crestWidth * 0.75f,
            breakingPhase
        );

    float crestCenter =
        glm::mix(
            -1.0f,
             2.0f,
            breakingPhase
        );

    for(uint32_t x = 0; x < config.distanceResolution; x++){
        float u =
            static_cast<float>(x) /
            static_cast<float>(config.distanceResolution - 1);

        float s =
            (u * 2.0f - 1.0f) *
            config.profileHalfWidth;

        float crest =
            animatedHeight *
            water::BoreProfileGaussian(
                s,
                crestCenter,
                animatedWidth
            );

        float rearTrough =
            -config.rearTroughDepth *
            life *
            water::BoreProfileGaussian(
                s,
                -8.0f,
                9.0f
            );

        float behindMask =
            1.0f -
            water::BoreProfileSmoothStep(
                -1.0f,
                 1.0f,
                 s
            );

        float trailEnvelope =
            behindMask *
            std::exp(
                std::min(s, 0.0f) /
                config.trailDecayLength
            );

        float trail =
            config.trailAmplitude *
            life *
            trailEnvelope *
            std::sin(
                glm::two_pi<float>() *
                s /
                config.trailWavelength
            );

        row.upward[x] =
            crest +
            rearTrough +
            trail;

        row.forward[x] =
            config.forwardDisplacement *
            life *
            water::BoreProfileGaussian(
                s,
                crestCenter,
                animatedWidth * 1.15f
            );

        row.forward[x] -=
            0.25f *
            config.forwardDisplacement *
            life *
            water::BoreProfileGaussian(
                s,
                -10.0f,
                12.0f
            );

        float normalizedCrest =
            water::BoreProfileGaussian(
                s,
                crestCenter,
                animatedWidth * 1.3f
            );

        row.crestMask[x] =
            water::BoreProfileSmoothStep(
                0.15f,
                0.8f,
                normalizedCrest
            ) * life;

        float rearFoam =
            0.45f *
            water::BoreProfileGaussian(
                s,
                -10.0f,
                16.0f
            );

        row.foamSource[x] =
            glm::clamp(
                row.crestMask[x] * breakingPhase +
                rearFoam * breakingPhase,
                0.0f,
                1.0f
            );
    }

    return row;
}
}

// 根据配置参数 config 和指定的 phase（动画相位，范围 [0, 1]），生成一张 一维纹理（height = 1），
// 其中 width 个采样点沿“距离轴”排列，每个点存储位移和导数的 vec4。
BoreWaveProfileData GenerateStaticBoreWaveProfile(
    const BoreWaveProfileConfig& config,
    float phase
)
{
    if(config.distanceResolution < 3){
        throw std::runtime_error("Bore profile distance resolution must be at least 3");
    }

    if(config.profileHalfWidth <= 0.0f){
        throw std::runtime_error("Bore profile half width must be positive");
    }

    BoreWaveProfileData result{};
    result.width = config.distanceResolution;
    result.height = 1;
    result.profileHalfWidth = config.profileHalfWidth;
    result.duration = config.duration;
    result.displacement.resize(result.width);
    result.derivative.resize(result.width);

    std::vector<float> forward(result.width, 0.0f);
    std::vector<float> upward(result.width, 0.0f);
    std::vector<float> foamSource(result.width, 0.0f);
    std::vector<float> crestMask(result.width, 0.0f);

    float crestCenter =
        (phase - 0.55f) * 4.0f;

    for(uint32_t x = 0; x < result.width; x++){
        float u =
            static_cast<float>(x) /
            static_cast<float>(result.width - 1);

        float s =
            (u * 2.0f - 1.0f) *
            config.profileHalfWidth;

        float crest =
            config.crestHeight *
            BoreProfileGaussian(
                s,
                crestCenter,
                config.crestWidth
            );

        float rearTrough =
            -config.rearTroughDepth *
            BoreProfileGaussian(
                s,
                -8.0f,
                9.0f
            );

        float behindMask =
            1.0f -
            BoreProfileSmoothStep(
                -1.0f,
                1.0f,
                s
            );

        float trailEnvelope =
            behindMask *
            std::exp(
                std::min(s, 0.0f) /
                config.trailDecayLength
            );

        float trail =
            config.trailAmplitude *
            trailEnvelope *
            std::sin(
                glm::two_pi<float>() *
                s /
                config.trailWavelength
            );

        upward[x] =
            crest +
            rearTrough +
            trail;

        forward[x] =
            config.forwardDisplacement *
            BoreProfileGaussian(
                s,
                crestCenter,
                config.crestWidth * 1.15f
            );

        forward[x] -=
            0.25f *
            config.forwardDisplacement *
            BoreProfileGaussian(
                s,
                -10.0f,
                12.0f
            );

        float normalizedCrest =
            BoreProfileGaussian(
                s,
                crestCenter,
                config.crestWidth * 1.3f
            );

        crestMask[x] =
            BoreProfileSmoothStep(
                0.15f,
                0.8f,
                normalizedCrest
            );

        float rearFoam =
            0.45f *
            BoreProfileGaussian(
                s,
                -10.0f,
                16.0f
            );

        foamSource[x] =
            glm::clamp(
                crestMask[x] + rearFoam,
                0.0f,
                1.0f
            );
    }

    float ds =
        2.0f *
        config.profileHalfWidth /
        static_cast<float>(config.distanceResolution - 1);

    for(uint32_t x = 0; x < result.width; x++){
        float dForwardDs = 0.0f;
        float dUpwardDs = 0.0f;

        if(x > 0 && x + 1 < result.width){
            dForwardDs =
                (forward[x + 1] - forward[x - 1]) /
                (2.0f * ds);

            dUpwardDs =
                (upward[x + 1] - upward[x - 1]) /
                (2.0f * ds);
        }

        float flowSpeed =
            config.baseFlowSpeed +
            config.crestFlowBoost *
            crestMask[x];

        float breakingWeight =
            crestMask[x];

        result.displacement[x] =
            glm::vec4(
                forward[x],
                upward[x],
                foamSource[x],
                crestMask[x]
            );

        result.derivative[x] =
            glm::vec4(
                dForwardDs,
                dUpwardDs,
                flowSpeed,
                breakingWeight
            );
    }

    return result;
}

BoreWaveProfileData GenerateAnimatedBoreWaveProfile(
    const BoreWaveProfileConfig& config
)
{
    if(config.distanceResolution < 3){
        throw std::runtime_error("Bore profile distance resolution must be at least 3");
    }

    if(config.phaseResolution < 2){
        throw std::runtime_error("Bore profile phase resolution must be at least 2");
    }

    if(config.profileHalfWidth <= 0.0f){
        throw std::runtime_error("Bore profile half width must be positive");
    }

    BoreWaveProfileData result{};
    result.width = config.distanceResolution;
    result.height = config.phaseResolution;
    result.profileHalfWidth = config.profileHalfWidth;
    result.duration = config.duration;

    size_t pixelCount =
        static_cast<size_t>(result.width) *
        static_cast<size_t>(result.height);

    result.displacement.resize(pixelCount);
    result.derivative.resize(pixelCount);

    float ds =
        2.0f *
        config.profileHalfWidth /
        static_cast<float>(config.distanceResolution - 1);

    for(uint32_t y = 0; y < result.height; y++){
        float v =
            static_cast<float>(y) /
            static_cast<float>(result.height - 1);

        BoreProfileRowFields row =
            GenerateProfileRow(config, v);

        for(uint32_t x = 0; x < result.width; x++){
            size_t index =
                static_cast<size_t>(y) *
                result.width +
                x;

            float dForwardDs = 0.0f;
            float dUpwardDs = 0.0f;

            if(x > 0 && x + 1 < result.width){
                dForwardDs =
                    (row.forward[x + 1] - row.forward[x - 1]) /
                    (2.0f * ds);

                dUpwardDs =
                    (row.upward[x + 1] - row.upward[x - 1]) /
                    (2.0f * ds);
            }

            float u =
                static_cast<float>(x) /
                static_cast<float>(result.width - 1);

            float s =
                (u * 2.0f - 1.0f) *
                config.profileHalfWidth;

            float backMask =
                1.0f -
                BoreProfileSmoothStep(
                    -2.0f,
                     2.0f,
                     s
                );

            float flowSpeed =
                config.baseFlowSpeed *
                backMask +
                config.crestFlowBoost *
                row.crestMask[x];

            float breakingPhase =
                BoreProfileSmoothStep(
                    0.35f,
                    0.65f,
                    v
                ) *
                (
                    1.0f -
                    BoreProfileSmoothStep(
                        0.82f,
                        1.0f,
                        v
                    )
                );

            float breakingWeight =
                glm::clamp(
                    row.crestMask[x] *
                    breakingPhase,
                    0.0f,
                    1.0f
                );

            result.displacement[index] =
                glm::vec4(
                    row.forward[x],
                    row.upward[x],
                    row.foamSource[x],
                    row.crestMask[x]
                );

            result.derivative[index] =
                glm::vec4(
                    dForwardDs,
                    dUpwardDs,
                    flowSpeed,
                    breakingWeight
                );
        }
    }

    return result;
}

}