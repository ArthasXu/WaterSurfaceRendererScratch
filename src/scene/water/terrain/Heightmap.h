#pragma once

#include <glm/glm.hpp>
#include <string>
#include <vector>

namespace water
{
struct Heightmap
{
    int width = 0;
    int height = 0;
    std::vector<float> heights;   // 归一化 [0,1]，行主序

    bool valid() const { return width > 0 && height > 0; }
    float Sample(float u, float v) const;  // uv∈[0,1] 双线性
};

Heightmap LoadHeightmap(const std::string& path);
}