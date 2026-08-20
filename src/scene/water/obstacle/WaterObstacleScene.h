#pragma once

#include "scene/water/obstacle/WaterObstacleTypes.h"

#include <filesystem>
#include <vector>

namespace water
{
class WaterObstacleScene
{
public:
    WaterObstacleAuthoring& AddCircle();
    WaterObstacleAuthoring& AddBox();
    WaterObstacleAuthoring& AddCapsule();

    void Duplicate(size_t index);
    void Delete(size_t index);
    void Clear();

    std::vector<WaterObstacleAuthoring>& Obstacles();
    const std::vector<WaterObstacleAuthoring>& Obstacles() const;

    bool SaveJson(
        const std::filesystem::path& path
    ) const;

    bool LoadJson(
        const std::filesystem::path& path
    );

private:
    uint32_t AllocateId();

private:
    std::vector<WaterObstacleAuthoring> m_Obstacles;
    uint32_t m_NextId = 1;
};
}