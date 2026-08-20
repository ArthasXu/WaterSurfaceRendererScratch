#pragma once

#include <glm/glm.hpp>

#include <cstdint>
#include <filesystem>
#include <vector>

namespace water
{
struct InteractionDomainAsset
{
    glm::vec2 worldMin{0.0f};
    glm::vec2 worldSize{0.0f};

    uint32_t resolution = 0;

    std::vector<float> groundHeight;
    std::vector<float> obstacleSDF;
    std::vector<glm::vec2> obstacleNormal;

    std::vector<uint8_t> solidFraction;
    std::vector<uint8_t> materialId;

    std::vector<float> drag;
    std::vector<float> foamGain;
    std::vector<float> sedimentGain;
};

bool SaveInteractionDomainAsset(
    const std::filesystem::path& path,
    const InteractionDomainAsset& asset
);

bool LoadInteractionDomainAsset(
    const std::filesystem::path& path,
    InteractionDomainAsset& asset
);
}