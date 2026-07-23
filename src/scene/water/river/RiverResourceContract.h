#pragma once

#include <glm/glm.hpp>

namespace water
{
// ===== 河流场 UBO =====
// 将河流场的世界空间范围和涌潮参数传递给着色器
struct alignas(16) RiverFieldUBO
{
    glm::vec4 domain;      // (worldMin.x, worldMin.y, worldSize, 1.0/worldSize)
    glm::vec4 bore;        // (boreSpeed, riverLength, bankFadeStart, bankFadeDistance)
};
}