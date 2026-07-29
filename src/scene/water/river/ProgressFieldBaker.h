#pragma once

#include "scene/water/river/ProgressFieldTypes.h"
#include "scene/water/river/RiverSpline.h"

namespace water
{
ProgressFieldData BakeProgressField(
    const RiverFieldConfig& config,
    const RiverSpline& spline
);
}