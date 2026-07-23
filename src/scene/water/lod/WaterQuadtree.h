#pragma once

#include "scene/water/lod/WaterQuadtreeTypes.h"

#include <glm/glm.hpp>

#include <array>
#include <vector>
#include <functional>

namespace water
{
// ===== 四叉树整体配置参数 =====
struct WaterQuadtreeConfig
{
    glm::vec2 rootCenter{0.0f};       // 四叉树根节点的世界空间中心坐标
    float rootSize = 2048.0f;         // 根节点边长（米），整个四叉树覆盖范围

    uint32_t maxLevel = 6;            // 最大细分层级（最细粒度），对应 2048 / 2^6 = 32m 的 Tile
    uint32_t patchCellCount = 32;     // 每个 Tile 的网格单元格数（共享网格为 32×32 cells）

    float fovYRadians = glm::radians(45.0f); // 垂直视场角（弧度），用于屏幕空间误差计算
    float splitPixels = 9.0f;         // 分裂阈值：当每个网格单元投影到屏幕的像素数 > 此值时，该 Tile 分裂
    float mergePixels = 6.0f;         // 合并阈值：当像素数 < 此值时，该 Tile 应合并（通常比 splitPixels 小以避免抖动）

    float minY = -15.0f;              // 水面 AABB 的最小 Y 值（用于视锥体裁剪，包含波谷和裙边）
    float maxY = 20.0f;               // 水面 AABB 的最大 Y 值（包含波峰和涌潮）

    std::function<bool(WaterTile&)> classifyTile;
    std::function<uint32_t(const WaterTile&)> requiredLevel;
};

// ===== 单个视锥体平面方程 =====
// 平面方程：dot(normal, point) + distance = 0
struct FrustumPlane
{
    glm::vec3 normal{0.0f};      // 平面法向量（指向视锥体内侧）
    float distance = 0.0f;       // 平面方程的常数项
};

// ===== 水面 LOD 四叉树管理类 =====
// 每帧根据相机位置和视锥体动态构建可见 Tile 列表
class WaterQuadtree
{
public:
    explicit WaterQuadtree(
        const WaterQuadtreeConfig& config
    );

    // 每帧调用一次：根据当前相机和投影信息重建可见 Tile 列表
    void Build(
        const glm::vec3& cameraPosition,
        const glm::mat4& viewProjection,
        uint32_t viewportHeight
    );

    // 获取上一帧 Build 后生成的所有可见 Tile（用于 Draw）
    const std::vector<WaterTile>& GetVisibleTiles() const;

private:
    // 递归构建 Tile 树：从给定 Tile 开始，根据是否分裂决定继续递归或加入可见列表
    void BuildRecursive(
        const WaterTile& tile,
        const glm::vec3& cameraPosition,
        uint32_t viewportHeight
    );

    // 判断一个 Tile 是否应该继续分裂（使用屏幕空间误差 + 距离保底规则）
    bool ShouldSplit(
        const WaterTile& tile,
        const glm::vec3& cameraPosition,
        uint32_t viewportHeight
    ) const;

    // 计算相机到 Tile 的最近距离（用于 LOD 选择和距离保底）
    float DistanceToTile(
        const WaterTile& tile,
        const glm::vec3& cameraPosition
    ) const;

    // 判断一个 Tile 是否在视锥体内（轴对齐包围盒 vs 视锥体平面）
    bool IsTileVisible(
        const WaterTile& tile
    ) const;

    // 从 ViewProjection 矩阵中提取 6 个视锥体平面
    void ExtractFrustumPlanes(
        const glm::mat4& viewProjection
    );

    // 创建单个 Tile 的工厂函数（设置默认值）
    WaterTile MakeTile(
        uint32_t level,
        uint32_t x,
        uint32_t z,
        const glm::vec2& worldMin,
        float worldSize
    ) const;

private:
    WaterQuadtreeConfig m_Config{};                  // 四叉树配置
    std::array<FrustumPlane, 6> m_FrustumPlanes{};   // 当前帧的 6 个视锥体平面（上下左右前后）
    std::vector<WaterTile> m_VisibleTiles;           // 当前帧可见的 Tile 列表
};
}