#pragma once

#include <glm/glm.hpp>

namespace water
{
struct alignas(16) BoreProfileUBO
{
    glm::vec4 domain;
    glm::vec4 animation;
    glm::vec4 geometry;
    glm::vec4 suppression;
};
}