#pragma once

#include <glm/glm.hpp>

namespace water
{
// 波前配置参数（BoreFrontParams）
// 这是用户可调的宏观控制参数，描述了一个直线型涌潮前线的完整初始状态。
struct BoreFrontParams
{
    glm::vec2 origin{0.0f}; // 波前线在世界空间中的基准点，通常位于前线的中点或起始点。

    glm::vec2 direction{1.0f, 0.0f}; // 波前线的推进方向（单位向量）。与波前线垂直，指向波浪传播的方向。

    float speed = 32.0f; // 	波前线沿 direction 方向前进的速度（米/秒）。
    float frontLength = 1000.0f; // 波前线的总长度（米）。即潮头在水平面上的延伸长度（例如 1000m）。

    float initialOffset = -100.0f; // 初始时刻波前线相对于 origin 在 direction 方向上的偏移量（米）。用于控制涌潮的初始位置。负值表示开始时潮头还未来到原点。

    float edgeFadeFraction = 0.03f; // 波前线两端的淡入淡出比例（相对于 frontLength）。值为 0.03 表示两端各 3% 长度用于平滑过渡，避免潮头在两端生硬消失。
};

// 空间点采样结果（BoreFrontSample）
// 对于给定的世界空间水平坐标 worldXZ，BoreFrontField 会计算该点相对于波前线的几何关系，并将结果封装在此结构体中。
struct BoreFrontSample
{
    float alongFront = 0.0f; // 该点在波前线切向的投影坐标（米）。原点位于波前线的起点（由 origin 和 direction 及 frontLength 决定）
    float crossFront = 0.0f; // 该点在波前线法向（即 direction 方向）的投影坐标（米）。正值表示在波前线前方（传播方向），负值表示后方。

    float frontU = 0.0f; // 标准化到 [0, 1] 的沿波前线坐标，用于采样 Front LUT。公式为 alongFront / frontLength + 0.5，使 0 对应起点，1 对应终点
    float frontUClamped = 0.0f; // frontU 被夹紧到 [0, 1] 后的值，用于安全采样纹理

    float frontPosition = 0.0f; // 当前时刻波前线的法向位置（米）。由 speed * time + initialOffset + offsetCurve(frontU) 计算，即波前线随时间向前推进
    float signedDistance = 0.0f; // 该点到波前线的带符号距离（米），正值表示在波前前方（尚未到达），负值表示在波前后方（已过去）。计算为 crossFront - frontPosition。

    float lengthMask = 0.0f; // 波前线长度淡入淡出掩码，用于平滑两端。通过 smoothstep 在 frontU 的 [0, edgeFade] 和 [1-edgeFade, 1] 处实现

    glm::vec2 localFrontNormal{1.0f, 0.0f}; // 局部波前线的法线方向（单位向量）。通常直接取自 direction，但在弯曲波前线或导数修正后可能变化

    float amplitudeMultiplier = 1.0f; // 从 Front LUT 的 G 通道读取的波峰振幅缩放系数，用于调整该点的波高。
    float foamMultiplier = 1.0f; // 从 Front LUT 的 B 通道读取的泡沫强度系数，用于控制该点的泡沫生成量。
    float profilePhaseOffset = 0.0f; // 从 Front LUT 的 A 通道读取的轮廓相位偏移，用于让 Wave Profile 的动画沿波前线产生变化，避免重复感。
};

}