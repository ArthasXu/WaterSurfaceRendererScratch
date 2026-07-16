#pragma once

#include "scene/water/bore/BoreProfileTypes.h"

namespace water
{
float BoreProfileSmoothStep(float edge0, float edge1, float x);
float BoreProfileGaussian(float x, float center, float width);

BoreWaveProfileData GenerateStaticBoreWaveProfile(
    const BoreWaveProfileConfig& config,
    float phase
);

BoreWaveProfileData GenerateAnimatedBoreWaveProfile(
    const BoreWaveProfileConfig& config
);
}