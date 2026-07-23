#include "scene/water/river/RiverField.h"

#include <algorithm>

namespace water
{
// 构造函数：保存预烘焙的河流场数据
RiverField::RiverField(
    RiverFieldData data
)
    : m_Data(std::move(data))
{
}

// 获取原始河流场数据的只读引用
const RiverFieldData& RiverField::GetData() const
{
    return m_Data;
}

// 最近邻采样 Flow Map
// 根据世界坐标查询 (flowDirection.x, flowDirection.z, boreAmplitudeScale, waterMask)
glm::vec4 RiverField::SampleFlowNearest(
    const glm::vec2& worldXZ
) const
{
    return m_Data.flow[WorldToIndex(worldXZ)];
}

// 最近邻采样 Coordinate Map
// 根据世界坐标查询 (normalizedProgress, normalizedLateral, normalizedBankDistance, curvatureWeight)
glm::vec4 RiverField::SampleCoordinateNearest(
    const glm::vec2& worldXZ
) const
{
    return m_Data.coordinate[WorldToIndex(worldXZ)];
}

RiverProjection RiverField::SampleProjectionNearest(
    const glm::vec2& worldXZ
) const
{
    glm::vec4 flow =
        SampleFlowNearest(worldXZ);

    glm::vec4 coord =
        SampleCoordinateNearest(worldXZ);

    RiverProjection projection{};
    projection.valid = flow.a > 0.01f;
    projection.tangent =
        glm::normalize(
            glm::vec2(flow.x, flow.y)
        );
    projection.progressMeters =
        coord.r * m_Data.riverLength;
    projection.lateralMeters =
        coord.g;
    projection.boreAmplitude =
        flow.b;
    projection.curvatureWeight =
        coord.a;

    return projection;
}

float RiverField::SampleWaterMask(
    const glm::vec2& worldXZ
) const
{
    return SampleFlowNearest(worldXZ).a;
}

float RiverField::SampleBoreAmplitude(
    const glm::vec2& worldXZ
) const
{
    return SampleFlowNearest(worldXZ).b;
}

// 将世界空间坐标转换为像素数组的线性索引。
// 映射关系：worldXZ → 归一化 UV → 像素坐标 (x,y) → 一维数组索引。
// 使用最近邻（无插值），坐标超出范围时钳位到边界像素。
uint32_t RiverField::WorldToIndex(
    const glm::vec2& worldXZ
) const
{
    // 计算世界坐标在纹理中的归一化 UV 坐标
    glm::vec2 uv =
        (worldXZ - m_Data.config.worldMin) /
        m_Data.config.worldSize;

    // 钳位到 [0, 1) 范围内，防止数组越界
    uv =
        glm::clamp(
            uv,
            glm::vec2(0.0f),
            glm::vec2(0.999999f)
        );

    // 映射到像素坐标
    uint32_t x =
        static_cast<uint32_t>(
            uv.x *
            static_cast<float>(m_Data.config.resolution)
        );

    uint32_t y =
        static_cast<uint32_t>(
            uv.y *
            static_cast<float>(m_Data.config.resolution)
        );

    // 双重保险：确保像素坐标不超出数组范围
    x = std::min(x, m_Data.config.resolution - 1);
    y = std::min(y, m_Data.config.resolution - 1);

    // 转换为行优先的一维数组索引
    return y * m_Data.config.resolution + x;
}
}