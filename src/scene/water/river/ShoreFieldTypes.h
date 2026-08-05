#pragma once

#include "scene/water/river/RiverFieldTypes.h"

#include <glm/glm.hpp>
#include <vector>

namespace water
{
// 岸线烘焙的可调参数（后续可接入美术工具）
struct ShoreFieldParams
{
    float wetRunup = 40.0f;       // 岸上湿润带最大延伸距离(米)
    float sandWidth = 60.0f;      // 沙滩影响半径(米)，岸线两侧
    float beachSlope = 0.15f;     // 岸上地形坡度(占位地形用)
    float maxBeachHeight = 12.0f; // 岸上地形最大抬升(米)
    float terrainHeightScale = 40.0f;  // heightmap [0,1] → 米
    float riverBedDepth = 50.0f;       // 河道内河床相对水面下沉(米)
    float riverBedFade = 1500.0f;      // 河床由岸边(0)下沉到最深所需的横向距离(米)
};

// 岸线场：R=到岸有符号距离(米) G=wetnessBase B=sand A=terrainHeight
struct ShoreFieldData
{
    RiverFieldConfig config{};
    float riverLength = 0.0f;
    std::vector<glm::vec4> field;
};
}