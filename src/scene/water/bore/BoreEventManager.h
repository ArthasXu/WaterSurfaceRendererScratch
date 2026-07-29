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
    glm::vec4 crestNoiseA{3.0f, 0.03f, 0.02f, 0.06f}; // x=横向频率 y=沿河频率X z=沿河频率Y w=动画速度
    glm::vec4 crestNoiseB{5.0f, 0.35f, 0.5f, 1.5f};   // x=细节频率 y=细节权重 z=振幅下限 w=振幅上限
    glm::vec4 crestNoiseC{0.35f, 3.0f, 0.0f, 0.0f};   // x=顶抖强度 y=顶抖频率
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
