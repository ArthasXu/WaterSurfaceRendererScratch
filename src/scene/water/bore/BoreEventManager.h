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
    // 浪脊噪声参数(仅顶点着色器读取；compute 着色器声明较短的块，不受影响)
    glm::vec4 crestNoiseA{2.0f, 0.01f, 0.008f, 0.06f}; // 横向频率 12→2：波长拉长到几百米
    glm::vec4 crestNoiseB{5.0f, 0.0f, 0.75f, 1.25f};   // 细节权重→0；振幅范围收窄到 0.75~1.25
    glm::vec4 crestNoiseC{0.15f, 0.3f, 0.0f, 0.0f};    // 顶抖强度 0.35→0.15，频率 3→0.3
    // x = 历史最远潮头推进距离(米)，单调不减；用于潮后水位永久保持
    glm::vec4 persistent{-1.0e9f, 0.85f, 0.18f, 0.0f}; // x=历史最远推进(米) y=横向覆盖[0..1] z=两岸淡出[0..1]
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
