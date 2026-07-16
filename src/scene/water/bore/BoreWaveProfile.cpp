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
}