#include "scene/water/river/RiverFieldBaker.h"

#include <algorithm>

namespace water
{
namespace
{
// 平滑阶跃函数，用于生成柔和的河岸过渡，避免硬边缘锯齿
float SmoothStep(
    float edge0,
    float edge1,
    float x
)
{
    float t =
        glm::clamp(
            (x - edge0) /
                (edge1 - edge0),
            0.0f,
            1.0f
        );

    return t * t * (3.0f - 2.0f * t);
}
}

// 烘焙河流场纹理：将 RiverSpline 定义的河流曲线“烘”成两张可采样的纹理。
// Flow Map 提供流向和掩码，Coordinate Map 提供河道局部坐标。
// 这两张纹理让着色器无需实时遍历样条线就能知道任意世界坐标点的河流信息。
RiverFieldData BakeRiverField(
    const RiverFieldConfig& config,
    const RiverSpline& spline
)
{
    RiverFieldData data{};
    data.config = config;
    data.riverLength = spline.GetLength();

    uint32_t pixelCount =
        config.resolution *
        config.resolution;

    // 预分配像素数组
    data.flow.resize(pixelCount);
    data.coordinate.resize(pixelCount);

    // 遍历每个像素
    for(uint32_t y = 0; y < config.resolution; ++y){
        for(uint32_t x = 0; x < config.resolution; ++x){
            // 像素中心对应的纹理坐标
            float u =
                (static_cast<float>(x) + 0.5f) /
                static_cast<float>(config.resolution);

            float v =
                (static_cast<float>(y) + 0.5f) /
                static_cast<float>(config.resolution);

            // 纹理坐标转换为世界空间坐标
            glm::vec2 worldXZ =
                config.worldMin +
                glm::vec2(u, v) *
                    config.worldSize;

            // 将该世界坐标点投影到河流中轴线上
            RiverProjection projection =
                spline.Project(worldXZ);

            // 像素在一维数组中的索引
            uint32_t index =
                y * config.resolution + x;

            // 投影无效或河流参数异常 → 填充默认值（表示非水域）
            if(!projection.valid ||
                projection.halfWidth <= 0.001f ||
                data.riverLength <= 0.001f){
                // Flow Map 默认：流向 (1,0)，振幅 1，水域掩码 0
                data.flow[index] =
                    glm::vec4(1.0f, 0.0f, 1.0f, 0.0f);

                // Coordinate Map 默认：全零
                data.coordinate[index] =
                    glm::vec4(0.0f);

                continue;
            }

            // ===== 计算到河岸的距离 =====
            // bankDistance > 0：在河道内（正值 = 到岸边的距离）
            // bankDistance < 0：在岸上（负值 = 超出河道的距离）
            float bankDistance =
                projection.halfWidth -
                std::abs(projection.lateralMeters);

            // ===== 水域掩码 =====
            // 用 smoothstep 在河岸附近生成柔和过渡，避免 fragment discard 产生锯齿
            float waterMask =
                SmoothStep(
                    0.0f,               // 不要让岸外也有半透明水域
                    config.bankFade,    // 正值：进入河道一定距离后完全为水域
                    bankDistance
                );

            // ===== Coordinate Map 各通道 =====

            // R：沿河归一化进度 [0, 1]。0 = 入海口，1 = 河道末端。
            float normalizedProgress =
                projection.progressMeters /
                data.riverLength;

            // G：横向归一化坐标 [-1, 1]。-1 = 左岸，0 = 中轴线，+1 = 右岸。
            float normalizedLateral =
                glm::clamp(
                    projection.lateralMeters /
                        projection.halfWidth,
                    -1.0f,
                    1.0f
                );

            // B：归一化河岸距离 [-1, 1]。负值 = 岸外，正值 = 河道中心区域。
            float normalizedBankDistance =
                glm::clamp(
                    bankDistance /
                        config.bankFadeDistance,
                    -1.0f,
                    1.0f
                );

            // ===== Flow Map 各通道 =====
            // R = 流向 X 分量
            // G = 流向 Z 分量
            // B = 涌潮振幅缩放系数
            // A = 水域掩码
            data.flow[index] =
                glm::vec4(
                    projection.tangent.x,
                    projection.tangent.y,
                    projection.boreAmplitude,
                    waterMask
                );

            // ===== Coordinate Map 各通道 =====
            // R = 归一化进度
            // G = 归一化横向坐标
            // B = 归一化河岸距离
            // A = 曲率权重
            data.coordinate[index] =
                glm::vec4(
                    normalizedProgress,
                    normalizedLateral,
                    normalizedBankDistance,
                    projection.curvatureWeight
                );
        }
    }

    return data;
}
}