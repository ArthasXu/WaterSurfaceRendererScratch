#pragma once

#include <glm/glm.hpp>

namespace water
{
struct alignas(16) BoreProfileUBO
{
    glm::vec4 distanceMapping;
    glm::vec4 waterRiseParams;
    glm::vec4 profileDebug;
    glm::vec4 animation;
};
}