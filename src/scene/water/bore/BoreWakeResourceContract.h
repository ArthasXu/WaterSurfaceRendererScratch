#pragma once

#include <glm/glm.hpp>

namespace water
{
struct alignas(16) BoreWakeParamsUBO
{
    glm::vec4 domain;       // xy=worldMin, z=worldSize, w=resolution
    glm::vec4 simulation;   // x=dt, y=time, z=enabled, w=sourceStrength
    glm::vec4 range;        // x=start, y=end, z=feather, w=advectionSpeed
    glm::vec4 decay;        // x=aeration, y=foam, z=sediment, w=turbulence
    glm::vec4 strength;     // x=aeration, y=foam, z=sediment, w=turbulence
    glm::vec4 noise;        // x=patchThreshold, y=warpStrength, z=lateralFreq, w=backFreq
};
}