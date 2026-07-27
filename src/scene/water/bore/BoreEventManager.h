#pragma once

#include <glm/glm.hpp>

#include <cstdint>
#include <random>
#include <vector>

namespace water
{
constexpr uint32_t kMaxBoreEvents = 16;

struct BoreEvent
{
    uint32_t id = 0;
    uint32_t seed = 0;

    float progressMeters = 0.0f;
    float speed = 8.0f;
    float age = 0.0f;

    float amplitudeScale = 1.0f;
    float widthScale = 1.0f;
    float forwardScale = 1.0f;

    float curvatureScale = 1.0f;
    float foamScale = 1.0f;
    float profilePhaseOffset = 0.0f;
    float variationPhase = 0.0f;

    bool active = false;
};

struct alignas(16) BoreEventGPU
{
    glm::vec4 motion;
    glm::vec4 shape;
    glm::vec4 appearance;
    glm::vec4 suppression;
};

struct alignas(16) MultiBoreUBO
{
    glm::ivec4 metadata;
    glm::vec4 river;
};

struct BoreEventManagerConfig
{
    bool enabled = true;
    float minSpawnInterval = 5.0f;
    float maxSpawnInterval = 10.0f;
    float retryMinInterval = 0.5f;
    float retryMaxInterval = 1.0f;
    float baseSpeed = 8.0f;
    float removeMargin = 120.0f;
    float minimumSeparationPadding = 10.0f;
};

class BoreEventManager
{
public:
    void Reset(uint32_t seed);

    void Update(
        float deltaTime,
        float riverLength,
        float profileHalfWidth,
        const BoreEventManagerConfig& config
    );

    const std::vector<BoreEvent>& GetActiveEvents() const;
    uint32_t GetActiveCount() const;
    float GetSpawnCountdown() const;
    BoreEvent SpawnManual(const BoreEventManagerConfig& config);

private:
    void TrySpawn(
        float profileHalfWidth,
        const BoreEventManagerConfig& config
    );

    BoreEvent CreateRandomEvent(const BoreEventManagerConfig& config);
    void ScheduleSpawn(float minSeconds, float maxSeconds);
    const BoreEvent* GetNewestActiveEvent() const;

private:
    std::mt19937 m_Random{1337u};
    float m_SpawnCountdown = 0.0f;
    uint32_t m_NextId = 1;
    std::vector<BoreEvent> m_Events;
};
}
