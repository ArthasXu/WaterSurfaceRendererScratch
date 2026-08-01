#include "scene/water/river/ShoreFieldBaker.h"

#include <algorithm>
#include <cmath>

namespace water
{
namespace
{
float SmoothStep(float edge0, float edge1, float x)
{
    float t = glm::clamp((x - edge0) / (edge1 - edge0), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}
}

ShoreFieldData BakeShoreField(
    const RiverFieldConfig& config,
    const RiverSpline& spline,
    const ShoreFieldParams& params,
    const Heightmap* heightmap
)
{
    ShoreFieldData data{};
    data.config = config;
    data.riverLength = spline.GetLength();
    data.field.resize(config.resolution * config.resolution);

    for(uint32_t y = 0; y < config.resolution; ++y){
        for(uint32_t x = 0; x < config.resolution; ++x){
            float u = (static_cast<float>(x) + 0.5f) / static_cast<float>(config.resolution);
            float v = (static_cast<float>(y) + 0.5f) / static_cast<float>(config.resolution);

            glm::vec2 worldXZ = config.worldMin + glm::vec2(u, v) * config.worldSize;

            RiverProjection projection = spline.Project(worldXZ);

            uint32_t index = y * config.resolution + x;

            // 投影无效 = 远离河道的深岸，用大负距离表示
            if(!projection.valid || projection.halfWidth <= 0.001f){
                data.field[index] = glm::vec4(-1000.0f, 0.0f, 0.0f,
                    params.maxBeachHeight);
                continue;
            }

            // 到岸的有符号距离：>0 河内，<0 岸上
            float bankDistance =
                projection.halfWidth - std::abs(projection.lateralMeters);

            // wetnessBase：河内=1，岸上在 wetRunup 内线性淡出
            float wetnessBase =
                1.0f - SmoothStep(0.0f, params.wetRunup, -bankDistance);

            // sand：岸线两侧一段范围内为 1
            float sand =
                1.0f - SmoothStep(0.0f, params.sandWidth, std::abs(bankDistance));

            // A：地形高度。优先用真实 heightmap，否则回退到线性坡度占位
            float terrainHeight;
            if(heightmap && heightmap->valid()){
                terrainHeight =
                    heightmap->Sample(u, v) * params.terrainHeightScale;
            } else {
                terrainHeight =
                    glm::min(std::max(0.0f, -bankDistance) * params.beachSlope,
                             params.maxBeachHeight);
            }
            // 河道内把河床压到水面下，保证水深为正（供 C 的吸收计算）
            if(bankDistance > 0.0f){
                terrainHeight -= params.riverBedDepth *
                    SmoothStep(0.0f, config.bankFade, bankDistance);
            }

            data.field[index] = glm::vec4(
                bankDistance,   // R
                wetnessBase,    // G
                sand,           // B
                terrainHeight   // A
            );
        }
    }

    return data;
}
}