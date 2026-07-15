#pragma once

#include "scene/water/bore/BoreFrontTypes.h"
#include "scene/water/bore/FrontParameterLUT.h"

#include <glm/glm.hpp>

namespace water
{
class BoreFrontField
{
public:
    explicit BoreFrontField(const BoreFrontParams& params);

    BoreFrontSample EvaluateStraight(
        glm::vec2 worldXZ,
        float timeSeconds
    ) const;

    BoreFrontSample Evaluate(
        glm::vec2 worldXZ,
        float timeSeconds,
        const FrontLUTData& lut
    ) const;

private:
    BoreFrontSample EvaluateBase(
        glm::vec2 worldXZ,
        float timeSeconds
    ) const;

private:
    BoreFrontParams m_Params;
};
}