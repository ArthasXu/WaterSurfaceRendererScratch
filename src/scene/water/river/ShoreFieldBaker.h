#pragma once

#include "scene/water/river/ShoreFieldTypes.h"
#include "scene/water/river/RiverSpline.h"

namespace water
{
ShoreFieldData BakeShoreField(
    const RiverFieldConfig& config,
    const RiverSpline& spline,
    const ShoreFieldParams& params
);
}