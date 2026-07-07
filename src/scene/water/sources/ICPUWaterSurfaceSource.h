#pragma once

#include "scene/water/common/WaterSurfaceTypes.h"

namespace water
{
class ICPUWaterSurfaceSource
{
public:
    virtual ~ICPUWaterSurfaceSource() = default;

    virtual void Update(float deltaTime) = 0; // 更新水面高度

    virtual WaterSurfaceSample Sample(glm::vec2 worldXZ) const = 0; // 用于采样水面高度
};
}