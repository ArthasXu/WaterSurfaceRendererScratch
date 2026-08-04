#include "scene/water/river/ProgressFieldBaker.h"

#include <algorithm>
#include <vector>

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

// 烘焙单张 Progress 贴图：R=progress, G=waterMask, B=amplitude, A=lateral
ProgressFieldData BakeProgressField(
    const RiverFieldConfig& config,
    const RiverSpline& spline
)
{
    ProgressFieldData data{};
    data.config = config;
    data.riverLength = spline.GetLength();

    uint32_t pixelCount =
        config.resolution *
        config.resolution;

    // 预分配像素数组
    data.field.resize(pixelCount);

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
                data.field[index] = glm::vec4(0.0f, 0.0f, 1.0f, 0.0f); // progress0, mask0, amp1, lateral0
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
                
            // ===== Coordinate Map 各通道 =====
            // R = 归一化进度
            // G = 水域掩码
            // B = 涌潮振幅缩放系数
            // A = 归一化横向坐标
            data.field[index] = glm::vec4(
                normalizedProgress,        // R
                waterMask,                 // G
                projection.boreAmplitude,  // B
                normalizedLateral          // A
            );

        }
    }

    // 弯道内侧存在最近点投影的中轴接缝，progress 场在此阶跃，
    // 会让波前穿过接缝时瞬间跳位。做几次 3x3 均值模糊把阶跃摊成缓坡。
    const int blurPasses = 6;
    const uint32_t res = config.resolution;
    std::vector<glm::vec4> temp(data.field.size());
    for(int pass = 0; pass < blurPasses; ++pass){
        for(uint32_t y = 0; y < res; ++y){
            for(uint32_t x = 0; x < res; ++x){
                glm::vec4 sum(0.0f);
                float weight = 0.0f;
                for(int dy = -1; dy <= 1; ++dy){
                    for(int dx = -1; dx <= 1; ++dx){
                        int sx = static_cast<int>(x) + dx;
                        int sy = static_cast<int>(y) + dy;
                        if(sx < 0 || sy < 0 ||
                           sx >= static_cast<int>(res) || sy >= static_cast<int>(res)){
                            continue;
                        }
                        sum += data.field[static_cast<uint32_t>(sy) * res + static_cast<uint32_t>(sx)];
                        weight += 1.0f;
                    }
                }
                temp[y * res + x] = sum / weight;
            }
        }
        data.field.swap(temp);
    }

    return data;
}
}