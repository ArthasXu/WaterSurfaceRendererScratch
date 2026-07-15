#pragma once

#include <glm/glm.hpp>

namespace water
{
// alignas(16) 是 C++11 的对齐说明符，它告诉编译器：这个结构体的首地址必须是 16 的倍数（即 16 字节对齐）
struct alignas(16) BoreFrontUBO
{
    glm::vec4 originSpeedTime;      // 波前原点位置和速度
    glm::vec4 directionLengthFade;  // 波前方向和长度
    glm::vec4 motionDebug;          // 波前运动调试
    glm::vec4 lutInfo;              // 波前LUT信息
};
}