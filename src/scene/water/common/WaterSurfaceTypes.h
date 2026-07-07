#pragma once

#include <glm/glm.hpp>

#include <cstdint> // uint32_t
#include <vector>

namespace water
{
struct WaterSurfaceSample
{
    float height = 0.0f; // 高度
    glm::vec2 horizontalDisplacement{0.0f}; // 水平位移
    glm::vec2 slope{0.0f}; // 斜率

    glm::vec2 velocity{0.0f}; // 速度

    float foamSource = 0.0f; // 泡沫源
    float blendMask = 1.0f; // 混合遮罩
}; // 用于存储水面采样点的信息

struct CPUWaterSurfaceFrame
{
    uint32_t resolution = 0; // 分辨率
    float patchLength = 0.0f; // 面片长度

    std::vector<glm::vec4> displacement;
    // R = horizontal displacement X，水平位移 X
    // G = vertical height，垂直高度
    // B = horizontal displacement Z，水平位移 Z
    // A = Jacobian / breaking hint。雅可比矩阵 / 断裂提示

    std::vector<glm::vec4> normalAux;
    // R = slope X，斜率 X
    // G = slope Z，斜率 Z
    // B = dDxdx
    // A = dDzdz
};
}