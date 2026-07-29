#pragma once

#include "scene/water/river/RiverFieldTypes.h"

#include <glm/glm.hpp>
#include <vector>

namespace water
{
// 单张河流 Progress 贴图数据（替代 Flow + Coordinate 两张图）
// R = normalizedProgress [0,1]   沿河里程表，潮头推进的唯一依据
// G = waterMask [0,1]            水域掩码，岸上=0，杀掉陆地上的浪
// B = boreAmplitude              涌潮振幅缩放（沿河变化）
// A = normalizedLateral [-1,1]   横向坐标，供噪声/岸边泡沫用
struct ProgressFieldData
{
    RiverFieldConfig config{};
    float riverLength = 0.0f;
    std::vector<glm::vec4> field;
};
}