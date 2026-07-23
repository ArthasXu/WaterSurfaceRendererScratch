#pragma once

#include "scene/water/river/RiverFieldTypes.h"
#include "scene/water/river/RiverSpline.h"

namespace water
{
RiverFieldData BakeRiverField(
    const RiverFieldConfig& config,
    const RiverSpline& spline
);
}