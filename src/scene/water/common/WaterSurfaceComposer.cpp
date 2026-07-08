#include "scene/water/common/WaterSurfaceComposer.h"

#include <algorithm>

namespace water
{
void WaterSurfaceComposer::AddSource(std::shared_ptr<ICPUWaterSurfaceSource> source)
{
    m_Sources.push_back(std::move(source));
}

void WaterSurfaceComposer::Update(float deltaTime)
{
    for(const std::shared_ptr<ICPUWaterSurfaceSource>& source : m_Sources){
        source->Update(deltaTime);
    }
}

WaterSurfaceSample WaterSurfaceComposer::Sample(glm::vec2 worldXZ) const
{
    WaterSurfaceSample result{};

    for(const std::shared_ptr<ICPUWaterSurfaceSource>& source : m_Sources){
        WaterSurfaceSample sample = source->Sample(worldXZ);

        result.height += sample.height;
        result.horizontalDisplacement += sample.horizontalDisplacement;
        result.slope += sample.slope;
        result.velocity += sample.velocity;
        result.foamSource = std::max(result.foamSource, sample.foamSource);
        result.blendMask *= sample.blendMask;
    }

    return result;
}
}