#pragma once

#include <glm/glm.hpp>

#include <cstdint>

namespace water
{
constexpr uint32_t kMaxFFTCascades = 3;

struct alignas(16) WaterParamsUBO
{
    glm::vec4 patchLengths;
    glm::vec4 amplitudeScales;
    glm::ivec4 metadata;
    glm::vec4 simulation;
};
}