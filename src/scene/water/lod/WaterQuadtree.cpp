#include "scene/water/lod/WaterQuadtree.h"

#include <algorithm>
#include <cmath>

namespace water
{
// 构造函数：保存配置
WaterQuadtree::WaterQuadtree(
    const WaterQuadtreeConfig& config
)
    : m_Config(config)
{
}

// 每帧主入口：重建可见 Tile 列表
void WaterQuadtree::Build(
    const glm::vec3& cameraPosition,
    const glm::mat4& viewProjection,
    uint32_t viewportHeight
)
{
    // 清空上一帧的结果
    m_VisibleTiles.clear();

    // 1. 从 ViewProjection 矩阵提取视锥体平面
    ExtractFrustumPlanes(viewProjection);

    // 2. 计算根 Tile 的左下角世界坐标
    glm::vec2 rootMin =
        m_Config.rootCenter -
        glm::vec2(m_Config.rootSize * 0.5f);

    // 3. 创建根 Tile（Level 0，覆盖整个水面范围）
    WaterTile root =
        MakeTile(
            0,              // level = 0
            0, 0,           // 根节点坐标为 (0,0)
            rootMin,        // 世界坐标左下角
            m_Config.rootSize // 边长
        );

    // 4. 从根开始递归构建
    BuildRecursive(root, cameraPosition, viewportHeight);
}

// 获取当前帧可见的所有 Tile
const std::vector<WaterTile>&
WaterQuadtree::GetVisibleTiles() const
{
    return m_VisibleTiles;
}

// 递归核心：对当前 Tile 判断是否可见、是否需要分裂
void WaterQuadtree::BuildRecursive(
    const WaterTile& tile,
    const glm::vec3& cameraPosition,
    uint32_t viewportHeight
)
{
    WaterTile classifiedTile = tile;

    if(m_Config.classifyTile){
        if(!m_Config.classifyTile(classifiedTile)){
            return; // 纯陆地 Tile，直接丢弃
        }
    }

    if(!IsTileVisible(classifiedTile)){
        return;  // 视锥体外，丢弃
    }

    // 判断是否需要分裂成 4 个子 Tile
    if(ShouldSplit(classifiedTile, cameraPosition, viewportHeight)){
        float childSize = classifiedTile.worldSize * 0.5f;       // 子 Tile 边长 = 父 Tile 一半
        uint32_t childLevel = classifiedTile.key.level + 1;      // 子层级 +1

        // 四叉分裂：2×2 四个子节点
        for(uint32_t z = 0; z < 2; ++z){
            for(uint32_t x = 0; x < 2; ++x){
                glm::vec2 childMin =
                    classifiedTile.worldMin +
                    glm::vec2(
                        static_cast<float>(x) * childSize,
                        static_cast<float>(z) * childSize
                    );

                WaterTile child =
                    MakeTile(
                        childLevel,
                        classifiedTile.key.x * 2 + x,    // 子节点 X 索引 = 父索引×2 + x
                        classifiedTile.key.z * 2 + z,    // 子节点 Z 索引 = 父索引×2 + z
                        childMin,
                        childSize
                    );

                // 递归处理子节点
                BuildRecursive(child, cameraPosition, viewportHeight);
            }
        }
        return;
    }

    // 不再分裂：将当前 Tile 标记为可见，加入绘制列表
    WaterTile visibleTile = classifiedTile;
    visibleTile.visible = true;
    m_VisibleTiles.push_back(visibleTile);
}

// 判断 Tile 是否应该分裂
bool WaterQuadtree::ShouldSplit(
    const WaterTile& tile,
    const glm::vec3& cameraPosition,
    uint32_t viewportHeight
) const
{
    // 已达到最大层级，不再分裂
    if(tile.key.level >= m_Config.maxLevel){
        return false;
    }

    // 强制河岸区域使用高 LOD
    if(m_Config.requiredLevel){
        uint32_t requiredLevel =
            m_Config.requiredLevel(tile);

        if(tile.key.level < requiredLevel){
            return true;
        }
    }

    // 计算相机到 Tile 的最近距离（至少取 1 米避免除零）
    float distance =
        std::max(
            DistanceToTile(tile, cameraPosition),
            1.0f
        );

    // 每个网格单元的世界空间尺寸
    float cellWorldSize =
        tile.worldSize /
        static_cast<float>(m_Config.patchCellCount);

    // 屏幕空间误差：该尺寸的网格单元投影到屏幕上的像素数
    float projectedPixels =
        cellWorldSize *
        static_cast<float>(viewportHeight) /
        (
            2.0f *
            std::tan(m_Config.fovYRadians * 0.5f) *
            distance
        );

    // 屏幕空间误差超过分裂阈值 → 分裂
    if(projectedPixels > m_Config.splitPixels){
        return true;
    }

    // 保底规则：极近距离强制达到高 LOD（避免因 projection 异常导致的低 LOD）
    if(distance < 100.0f && tile.key.level < 6)  return true;
    if(distance < 220.0f && tile.key.level < 5)  return true;
    if(distance < 450.0f && tile.key.level < 4)  return true;
    if(distance < 900.0f && tile.key.level < 3)  return true;

    return false;
}

// 计算相机到 Tile 的最近水平距离
float WaterQuadtree::DistanceToTile(
    const WaterTile& tile,
    const glm::vec3& cameraPosition
) const
{
    // Tile 的右上角世界坐标
    glm::vec2 tileMax =
        tile.worldMin +
        glm::vec2(tile.worldSize);

    // 相机在 XZ 平面上的位置
    glm::vec2 cameraXZ(cameraPosition.x, cameraPosition.z);

    // 将相机 XZ 坐标钳位到 Tile 的 AABB 内，得到 Tile 上离相机最近的点
    glm::vec2 closest =
        glm::clamp(
            cameraXZ,
            tile.worldMin,
            tileMax
        );

    // 返回相机到该最近点的水平距离
    return glm::length(cameraXZ - closest);
}

// 判断 Tile 是否在视锥体内（使用正顶点法）
bool WaterQuadtree::IsTileVisible(
    const WaterTile& tile
) const
{
    // Tile 的 3D AABB 最小点和最大点
    glm::vec3 minPoint(tile.worldMin.x, m_Config.minY, tile.worldMin.y);
    glm::vec3 maxPoint(tile.worldMin.x + tile.worldSize, m_Config.maxY, tile.worldMin.y + tile.worldSize);

    // 对每个视锥体平面，找到 AABB 上离平面最远的“正顶点”
    for(const FrustumPlane& plane : m_FrustumPlanes){
        glm::vec3 positive = minPoint;

        // 根据平面法线分量符号选择正顶点（使点乘结果最大）
        if(plane.normal.x >= 0.0f) positive.x = maxPoint.x;
        if(plane.normal.y >= 0.0f) positive.y = maxPoint.y;
        if(plane.normal.z >= 0.0f) positive.z = maxPoint.z;

        // 正顶点仍在平面外侧 → AABB 完全在视锥体外
        if(glm::dot(plane.normal, positive) + plane.distance < 0.0f){
            return false;
        }
    }

    // 所有平面都通过 → 可见
    return true;
}

// 从 ViewProjection 矩阵中提取 6 个视锥体平面
void WaterQuadtree::ExtractFrustumPlanes(
    const glm::mat4& viewProjection
)
{
    glm::mat4 m = glm::transpose(viewProjection);

    // 左平面：row3 + row0
    m_FrustumPlanes[0] = { glm::vec3(m[3] + m[0]), (m[3] + m[0]).w };
    // 右平面：row3 - row0
    m_FrustumPlanes[1] = { glm::vec3(m[3] - m[0]), (m[3] - m[0]).w };
    // 下平面：row3 + row1
    m_FrustumPlanes[2] = { glm::vec3(m[3] + m[1]), (m[3] + m[1]).w };
    // 上平面：row3 - row1
    m_FrustumPlanes[3] = { glm::vec3(m[3] - m[1]), (m[3] - m[1]).w };
    // 近平面：row2
    m_FrustumPlanes[4] = { glm::vec3(m[2]), m[2].w };
    // 远平面：row3 - row2
    m_FrustumPlanes[5] = { glm::vec3(m[3] - m[2]), (m[3] - m[2]).w };

    // 归一化所有平面
    for(FrustumPlane& plane : m_FrustumPlanes){
        float length = glm::length(plane.normal);
        if(length > 0.0f){
            plane.normal /= length;
            plane.distance /= length;
        }
    }
}

// 创建 Tile 的工厂函数
WaterTile WaterQuadtree::MakeTile(
    uint32_t level,
    uint32_t x,
    uint32_t z,
    const glm::vec2& worldMin,
    float worldSize
) const
{
    WaterTile tile{};
    tile.key.level = level;
    tile.key.x = x;
    tile.key.z = z;
    tile.worldMin = worldMin;
    tile.worldSize = worldSize;
    tile.visible = false;
    tile.intersectsWater = true;

    return tile;
}
}