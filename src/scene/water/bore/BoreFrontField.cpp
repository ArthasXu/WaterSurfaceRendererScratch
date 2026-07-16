#include "scene/water/bore/BoreFrontField.h"

#include <glm/common.hpp>
#include <glm/geometric.hpp>

#include <algorithm>
#include <stdexcept>
#include <cmath>

namespace
{
// GLSL smoothstep 的 CPU 端等价实现，用于在波前两端生成平滑淡入淡出掩码，避免潮头生硬消失
// 在 [edge0, edge1] 区间内生成一个 S 形曲线，输出 0 到 1 之间的平滑过渡值。
float SmoothStep(float edge0, float edge1, float x)
{
    if(std::abs(edge1 - edge0) < 1e-6f){
        return x >= edge1 ? 1.0f : 0.0f;
    }

    float t = (x - edge0) / (edge1 - edge0);
    t = std::clamp(t, 0.0f, 1.0f);

    return t * t * (3.0f - 2.0f * t);
}
}

namespace water
{
// t 时刻 xz 位置的浪有多大，是由 “预计算的静态空间特征” 和 “运行时动态变化的波形” 两层叠加在一起得到的。
// 最终浪高 = 波前振幅乘数（静态） × 当前翻卷波形的垂直位移（动态）
// 第一步：在 t 时刻找到波前线的位置，确定该点到波前线的距离 sdf
    // 波前线的位置由三部分组成（在 Evaluate 函数里完成）：
        // 基础推进：speed × time + initialOffset，波前线整体随时间的推进。
        // 弯曲修正：从 Front LUT 里查表得到的 frontOffset（R通道），叠加在基础推进位置上。这一步就把弯曲形状加到了波前线上。
        // 顶点到波前线的距离：s = 顶点在传播方向上的投影 - 波前线位置。得到的带符号距离 s，如果为正值，表示该点在波前前方（还没被潮头经过）；负值表示在波前后方（已经被潮头覆盖）。
// 第二步：获取该位置的空间特征（来自 Front LUT，预计算好的） 振幅乘数（G通道）和 泡沫乘数（B通道）
    // 这些是 LUT 里提前存好的静态空间分布，由河道地形或美术设计决定，运行时不会随时间改变。
// 第三步：获取动态的浪高（来自 Wave Profile 纹理，运行时采样）
    // 构造采样坐标：
        // U 轴 = 第一步里计算出的带符号距离 s，映射到 Wave Profile 纹理的水平轴。
        // U 轴决定了波形的横截面形状（前坡、波峰、后坡）。
        // V 轴 = 动画时间 t + 从 Front LUT 里拿到的 轮廓相位偏移（A通道）。
        // V 轴决定了潮头当前处于哪个动画阶段（形成、抬升、翻卷、破碎）。
    // 采样位移纹理：用这个 (U, V) 坐标去采样 Wave Profile 位移纹理，拿到：
        // R 通道：前向位移
        // G 通道：向上的浪高（也就是“浪多大”）
        // B 通道：泡沫源
        // A 通道：浪尖掩码
    // 计算最终浪高：最终的浪高 = G 通道的向上位移 × Front LUT 的振幅乘数。
BoreFrontField::BoreFrontField(const BoreFrontParams& params)
    : m_Params(params)
{
    if(m_Params.frontLength <= 0.0f){
        throw std::runtime_error("BoreFrontField frontLength must be positive");
    }

    if(glm::length(m_Params.direction) < 1e-6f){
        throw std::runtime_error("BoreFrontField direction must be non-zero");
    }

    if (m_Params.edgeFadeFraction < 0.0f ||
        m_Params.edgeFadeFraction > 0.5f)
    {
        throw std::runtime_error(
            "BoreFrontField edgeFadeFraction must be in [0, 0.5]"
        );
    }
}

// 基础几何采样
// 作用：计算指定世界坐标点与直线波前线的基本几何关系，这是所有后续计算的基础。
BoreFrontSample BoreFrontField::EvaluateBase(
    glm::vec2 worldXZ,
    float timeSeconds
) const
{
    glm::vec2 n = glm::normalize(m_Params.direction); // 波前法向 n

    glm::vec2 tangent{
        -n.y,
         n.x
    }; // 波前切线 tangent，与 n 垂直

    glm::vec2 relative =
        worldXZ - m_Params.origin; // 从波前线原点到目标点的相对位置向量

    float alongFront =
        glm::dot(relative, tangent); // 点到波前线切向的投影距离 alongFront，即点在波前线的水平位置

    float crossFront =
        glm::dot(relative, n); // 点到波前线法向的投影距离 crossFront，即点在波前线的垂直位置

    float frontU =
        alongFront / m_Params.frontLength + 0.5f; // 标准化到 [0, 1] 的沿波前线坐标 frontU，用于采样 Front LUT

    float frontUClamped =
        glm::clamp(frontU, 0.0f, 1.0f); // 夹紧到 [0, 1] 的 frontUClamped，用于安全采样纹理

    float frontPosition =
        m_Params.initialOffset +
        m_Params.speed * timeSeconds; // 当前时刻波前线的法向位置 frontPosition，由 speed * time + initialOffset 计算

    float signedDistance =
        crossFront - frontPosition; // 该点到波前线的带符号距离 signedDistance，正值表示在波前前方（尚未到达），负值表示在波前后方（已过去）

    float edgeFade =
        m_Params.edgeFadeFraction; // 波前线两端的淡入淡出比例 edgeFade，用于平滑过渡

    float leftMask =
        SmoothStep(
            0.0f,
            edgeFade,
            frontU
        ); // 左侧淡入淡出掩码 leftMask，在 [0, edgeFade] 区间内生成平滑过渡

    float rightMask =
        1.0f -
        SmoothStep(
            1.0f - edgeFade,
            1.0f,
            frontU
        ); // 右侧淡入淡出掩码 rightMask，在 [1-edgeFade, 1] 区间内生成平滑过渡

    BoreFrontSample sample{}; // 创建 BoreFrontSample 结构体，用于存储计算结果
    sample.alongFront = alongFront;
    sample.crossFront = crossFront;
    sample.frontU = frontU;
    sample.frontUClamped = frontUClamped;
    sample.frontPosition = frontPosition;
    sample.signedDistance = signedDistance;
    sample.lengthMask = leftMask * rightMask;
    sample.localFrontNormal = n;
    sample.amplitudeMultiplier = 1.0f;
    sample.foamMultiplier = 1.0f;
    sample.profilePhaseOffset = 0.0f;

    return sample;
}

// 直线波前采样 直接调用 EvaluateBase 进行纯几何采样，适用于不使用 LUT 或 Wave Profile 的简单直线涌潮
BoreFrontSample BoreFrontField::EvaluateStraight(
    glm::vec2 worldXZ,
    float timeSeconds
) const
{
    return EvaluateBase(worldXZ, timeSeconds);
}

// 完整 LUT(Look‑Up Table，查找表) 驱动采样 
// 在 EvaluateBase 基础上叠加 Front LUT 的修正，实现可弯曲、参数可调的波前形态。
BoreFrontSample BoreFrontField::Evaluate(
    glm::vec2 worldXZ,
    float timeSeconds,
    const FrontLUTData& lut
) const
{
    BoreFrontSample sample =
        EvaluateBase(worldXZ, timeSeconds); // 首先调用 EvaluateBase 进行基础几何采样

    glm::vec4 params =
        SampleLUTLinear(
            lut.parameters,
            sample.frontUClamped
        ); // 从 Front LUT 中采样参数，包括波前的振幅、速度、相位偏移等

    glm::vec4 derivative =
        SampleLUTLinear(
            lut.derivatives,
            sample.frontUClamped
        ); // 从 Front LUT 中采样导数，用于计算波前的切线方向

    float offsetMeters =
        params.r; // 从参数中获取波前的偏移量（米）

    sample.frontPosition += offsetMeters; // 更新波前线的位置，加上偏移量
    sample.signedDistance =
        sample.crossFront - sample.frontPosition; // 重新计算带符号距离

    glm::vec2 n =
        glm::normalize(m_Params.direction); // 波前法向 n

    glm::vec2 tangent{
        -n.y,
         n.x
    }; // 波前切线 tangent，与 n 垂直

    float derivativeWorld =
        derivative.r / m_Params.frontLength; // 从导数中获取波前切向的变化率，并转换为世界坐标单位

    glm::vec2 gradient =
        n - derivativeWorld * tangent; // 计算波前的梯度，即切线方向的变化率

    sample.localFrontNormal =
        glm::normalize(gradient); // 更新波前线的法向，使用梯度计算

    sample.amplitudeMultiplier = params.g; // 从参数中获取波前的振幅乘数
    sample.foamMultiplier = params.b; // 从参数中获取波前的泡沫乘数
    sample.profilePhaseOffset = params.a; // 从参数中获取波前的相位偏移

    return sample;
}

}