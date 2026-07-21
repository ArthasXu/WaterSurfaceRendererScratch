#pragma once

#include <glm/glm.hpp>

#include <cstdint>

namespace water
{
// ===== 四叉树 Tile 的唯一标识键 =====
// 通过 (level, x, z) 三元组唯一确定四叉树中的一个节点
struct WaterTileKey
{
    uint32_t level = 0;  // 四叉树层级（0 为根，值越大网格越细）
    uint32_t x = 0;      // 当前层级内的 X 方向索引（从左到右，0 ~ 2^level-1）
    uint32_t z = 0;      // 当前层级内的 Z 方向索引（从上到下，0 ~ 2^level-1）
};

// ===== 单个四叉树 Tile 的运行时数据 =====
// 每个 Tile 代表水面的一块正方形区域，包含其空间范围、LOD 状态和可见性信息
struct WaterTile
{
    WaterTileKey key{};          // 四叉树唯一标识

    // ---- 空间范围 ----
    glm::vec2 worldMin{0.0f};    // Tile 在世界空间中的左下角坐标 (x, z)
    float worldSize = 0.0f;      // Tile 在世界空间中的边长（米），正方形区域

    // ---- LOD 混合参数 ----
    uint32_t edgeMask = 0;       // 边掩码：标记该 Tile 的上下左右哪条边需要与更粗层级做 morph 过渡
    float morphAlpha = 0.0f;     // 顶点 morph 混合系数，用于在两个 LOD 层级之间平滑过渡

    // ---- 可见性与分类 ----
    bool visible = false;        // 当前帧是否通过视锥体裁剪，为 true 时才会被绘制
    bool intersectsWater = true; // 是否与水面区域相交（本阶段始终为 true，后续可结合河道 Mask 使用）
    bool intersectsBank = false; // 是否与河岸区域相交（暂保留，后续用于近岸特效）
    bool intersectsBore = false; // 是否与涌潮波前区域相交（暂保留，后续用于 Bore 相关 LOD 提升）
};

// ===== 单个 Tile 绘制时的 Push Constants =====
// 使用 Push Constants 可避免为每个 Tile 绑定独立的 UBO，减少 Draw Call 开销。
// 所有 Tile 共用同一个共享网格 (WaterPatchMesh)，通过 Push Constants 传入各自的变换参数。
struct WaterTilePushConstants
{
    glm::vec2 worldMin{0.0f};    // Tile 在世界空间中的左下角坐标 (x, z)，与 WaterTile::worldMin 对应
    float worldSize = 0.0f;      // Tile 在世界空间中的边长（米），与 WaterTile::worldSize 对应
    float morphAlpha = 0.0f;     // morph 混合系数，与 WaterTile::morphAlpha 对应

    uint32_t level = 0;          // 当前 Tile 的四叉树层级，着色器可用于调试或差异化处理
    uint32_t edgeMask = 0;       // 边掩码，与 WaterTile::edgeMask 对应
    uint32_t flags = 0;          // 预留的标志位，可用于传递布尔状态（如 isBoreTile 等）
    uint32_t padding = 0;        // 对齐填充，确保 Push Constants 总大小为 16 字节的整数倍
};
}