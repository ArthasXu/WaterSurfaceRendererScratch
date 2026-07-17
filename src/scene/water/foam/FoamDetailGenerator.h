#pragma once

#include "scene/water/foam/FoamTypes.h"

#include <string>

namespace water
{
// 用周期性噪声或周期性 Voronoi 算法预计算 FoamDetailTextureData
FoamDetailTextureData GenerateFoamDetailTexture(
    uint32_t width,
    uint32_t height,
    uint32_t seed
);

void WriteFoamDetailDebugPGMs(
    const std::string& directory,
    const FoamDetailTextureData& data
);
}