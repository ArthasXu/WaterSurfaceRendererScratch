#pragma once

#include "scene/water/sources/ICPUWaterSurfaceSource.h"

#include <memory>
#include <vector>

namespace water
{
class WaterSurfaceComposer
{
public:
    void AddSource(std::shared_ptr<ICPUWaterSurfaceSource> source);

    void Update(float deltaTime);

    WaterSurfaceSample Sample(glm::vec2 worldXZ) const;

private:
    std::vector<std::shared_ptr<ICPUWaterSurfaceSource>> m_Sources; // 波浪源
};
}