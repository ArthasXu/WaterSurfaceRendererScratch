#pragma once

#include <cstdint>
#include <vector>

namespace water
{
struct FoamDetailPixel
{
    uint8_t coverage = 0;    // 泡沫细胞覆盖度（0=无水花，255=满水花）
    uint8_t normalX = 128;   // 细节法线 X 分量（128 = 无偏转）
    uint8_t normalZ = 128;   // 细节法线 Z 分量（128 = 无偏转）
    uint8_t breakup = 0;     // 破碎噪声（用于破坏泡沫的规律性）
};

// FoamDetailTextureData 是 CPU 端生成的“无缝平铺泡沫细节纹理”的内存表示
struct FoamDetailTextureData
{
    uint32_t width = 0;      // 纹理宽度（如 256）
    uint32_t height = 0;     // 纹理高度（如 256）
    std::vector<FoamDetailPixel> pixels; // 像素数据
};
}