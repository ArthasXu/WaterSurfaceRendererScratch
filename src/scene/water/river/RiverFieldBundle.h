#pragma once

#include "scene/water/river/RiverFieldTypes.h"

#include <glm/glm.hpp>

#include <string>
#include <vector>

namespace water
{
// Serialized bundle of the 4 pre-baked river fields
// (Flow / Coordinate / Progress / Shore) stored as raw glm::vec4 arrays.
// Runtime only reads + packs + uploads, skipping the expensive
// per-pixel spline.Project baking.
struct RiverFieldBundle
{
    RiverFieldConfig config{};
    float riverLength = 0.0f;

    std::vector<glm::vec4> flow;        // RiverField Flow Map (packed to RGBA16F)
    std::vector<glm::vec4> coordinate;  // RiverField Coordinate Map (RGBA32F)
    std::vector<glm::vec4> progress;    // Progress Field (RGBA16F)
    std::vector<glm::vec4> shore;       // Shore Mask (RGBA16F)
};

// Write to disk (creates parent dirs). Returns true on success.
bool SaveRiverFieldBundle(
    const std::string& path,
    const RiverFieldBundle& bundle
);

// Read from disk. Returns true only if magic/version/resolution validate.
bool LoadRiverFieldBundle(
    const std::string& path,
    RiverFieldBundle& outBundle
);
}
