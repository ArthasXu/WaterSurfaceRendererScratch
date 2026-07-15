#pragma once

#include <glm/glm.hpp>

namespace water
{
struct BoreFrontParams
{
    glm::vec2 origin{0.0f};

    glm::vec2 direction{1.0f, 0.0f};

    float speed = 8.0f;
    float frontLength = 1000.0f;

    float initialOffset = -100.0f;

    float edgeFadeFraction = 0.03f;
};

struct BoreFrontSample
{
    float alongFront = 0.0f;
    float crossFront = 0.0f;

    float frontU = 0.0f;
    float frontUClamped = 0.0f;

    float frontPosition = 0.0f;
    float signedDistance = 0.0f;

    float lengthMask = 0.0f;

    glm::vec2 localFrontNormal{1.0f, 0.0f};

    float amplitudeMultiplier = 1.0f;
    float foamMultiplier = 1.0f;
    float profilePhaseOffset = 0.0f;
};

struct BoreFrontSample
{
    float alongFront = 0.0f;
    float crossFront = 0.0f;

    float frontU = 0.0f;
    float frontUClamped = 0.0f;

    float frontPosition = 0.0f;
    float signedDistance = 0.0f;

    float lengthMask = 0.0f;

    glm::vec2 localFrontNormal{1.0f, 0.0f};

    float amplitudeMultiplier = 1.0f;
    float foamMultiplier = 1.0f;
    float profilePhaseOffset = 0.0f;
};

}