#pragma once

#include "scene/water/river/RiverFieldTypes.h"

// CreateRiverResources
//     │
//     ├── RiverSpline.Build(控制点) → 河流中轴线几何数据
//     │
//     ├── BakeRiverField → CPU 端像素数组 (RiverFieldData)
//     │       │
//     │       ├──→ RiverField (CPU 端查询，用于四叉树剔除)
//     │       │
//     │       └──→ StaticDataTexture2D (上传为 GPU 纹理)
//     │               ├── Flow Map
//     │               └── Coordinate Map
//     │
//     └── 着色器采样 (texture(flowMap, uv) / texture(coordMap, uv))
//             │
//             └── 弯曲河道涌潮计算 (流向、进度、振幅、河岸距离等)

namespace water
{
// ===== CPU 端河流场查询类 =====
// 对 BakeRiverField 生成的预烘焙河流场数据进行 CPU 端查询。
// 主要用于：
//   - 四叉树构建时判断 Tile 是否与水域相交（粗粒度剔除）
//   - 离线验证和调试烘焙结果的正确性
// 运行时 GPU 端的水面渲染直接采样 Flow/Coordinate 纹理，不经过此类。
class RiverField
{
public:
    // 传入预烘焙完成的河流场数据（通常由 BakeRiverField 生成）
    explicit RiverField(
        RiverFieldData data
    );

    // 获取原始河流场数据的只读引用
    const RiverFieldData& GetData() const;

    // 最近邻采样 Flow Map：根据世界坐标查询 (流向.x, 流向.y, 振幅缩放, 水域掩码)
    glm::vec4 SampleFlowNearest(
        const glm::vec2& worldXZ
    ) const;

    // 最近邻采样 Coordinate Map：根据世界坐标查询 (进度, 横向坐标, 河岸距离, 曲率权重)
    glm::vec4 SampleCoordinateNearest(
        const glm::vec2& worldXZ
    ) const;

    RiverProjection SampleProjectionNearest(
        const glm::vec2& worldXZ
    ) const;

    float SampleWaterMask(
        const glm::vec2& worldXZ
    ) const;

    float SampleBoreAmplitude(
        const glm::vec2& worldXZ
    ) const;

private:
    // 将世界坐标转换为数据数组的线性索引（最近邻，无插值）
    uint32_t WorldToIndex(
        const glm::vec2& worldXZ
    ) const;

private:
    RiverFieldData m_Data{}; // 预烘焙的河流场数据（Flow Map + Coordinate Map 的像素数组）
};
}