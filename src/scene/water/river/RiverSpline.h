#pragma once

#include "scene/water/river/RiverFieldTypes.h"

#include <vector>

namespace water
{
// ===== 河流样条线 =====
// 将一系列控制点插值成平滑的河流中轴线，并提供空间投影功能。
// 空间投影是将任意世界坐标点映射到河流中轴线上的最近点——这是让涌潮波前
// 能够沿着弯曲河道传播的核心几何运算。
class RiverSpline
{
public:
    // 用控制点数组和每段采样数构建样条线。
    // controlPoints：定义河流走向、宽度、振幅等属性的控制点序列。
    // samplesPerSegment：相邻控制点之间插值产生的采样点数量。
    void Build(
        const std::vector<RiverControlPoint>& controlPoints,
        uint32_t samplesPerSegment
    );

    // 将世界空间中的一个点投影到河流中轴线上，返回最近点的局部坐标与属性。
    // 这是弯曲河道中计算 signedDistance、波前位置、振幅等的基础。
    RiverProjection Project(
        const glm::vec2& worldXZ
    ) const;

    // 返回河流中轴线的总长度（米）。
    float GetLength() const;

    // 返回插值后的所有采样点（用于调试或预烘焙纹理时遍历河流曲线）。
    const std::vector<RiverSamplePoint>& GetSamples() const;

private:
    // 对两个控制点做线性插值，生成中间采样点。
    RiverSamplePoint InterpolateControlPoints(
        const RiverControlPoint& a,
        const RiverControlPoint& b,
        float t
    ) const;

private:
    std::vector<RiverSamplePoint> m_Samples; // 插值后的河流中轴线采样点数组
    float m_Length = 0.0f;                   // 河流中轴线总长度（米）
};
}