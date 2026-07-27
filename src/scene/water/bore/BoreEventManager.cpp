#include "scene/water/bore/BoreEventManager.h"

#include <glm/gtc/constants.hpp>

#include <algorithm>

namespace water
{
void BoreEventManager::Reset(uint32_t seed)
{
    m_Random.seed(seed);
    m_SpawnCountdown = 0.0f;
    m_NextId = 1;
    m_Events.clear();
}

void BoreEventManager::Update(
    float deltaTime,
    float riverLength,
    float profileHalfWidth,
    const BoreEventManagerConfig& config
){
    if(!config.enabled){
        return;
    }

    for(BoreEvent& event : m_Events){
        if(!event.active){
            continue;
        }

        event.progressMeters += event.speed * deltaTime;
        event.age += deltaTime;

        if(event.progressMeters > riverLength + config.removeMargin){
            event.active = false;
        }
    }

    m_SpawnCountdown -= deltaTime;

    if(m_SpawnCountdown <= 0.0f){
        TrySpawn(profileHalfWidth, config);
    }
}

const std::vector<BoreEvent>& BoreEventManager::GetActiveEvents() const
{
    return m_Events;
}

uint32_t BoreEventManager::GetActiveCount() const
{
    uint32_t count = 0;

    for(const BoreEvent& event : m_Events){
        if(event.active){
            ++count;
        }
    }

    return count;
}

float BoreEventManager::GetSpawnCountdown() const
{
    return m_SpawnCountdown;
}

BoreEvent BoreEventManager::SpawnManual(const BoreEventManagerConfig& config)
{
    BoreEvent event = CreateRandomEvent(config);

    if(m_Events.size() < kMaxBoreEvents){
        m_Events.push_back(event);
    }
    else{
        auto inactive =
            std::find_if(
                m_Events.begin(),
                m_Events.end(),
                [](const BoreEvent& candidate){
                    return !candidate.active;
                }
            );

        if(inactive != m_Events.end()){
            *inactive = event;
        }
    }

    ScheduleSpawn(config.minSpawnInterval, config.maxSpawnInterval);
    return event;
}

void BoreEventManager::TrySpawn(
    float profileHalfWidth,
    const BoreEventManagerConfig& config
){
    const BoreEvent* newest = GetNewestActiveEvent();

    if(newest != nullptr){
        float minimumSeparation =
            2.0f * profileHalfWidth * newest->widthScale +
            config.minimumSeparationPadding;

        if(newest->progressMeters < minimumSeparation){
            ScheduleSpawn(config.retryMinInterval, config.retryMaxInterval);
            return;
        }
    }

    SpawnManual(config);
}

BoreEvent BoreEventManager::CreateRandomEvent(const BoreEventManagerConfig& config)
{
    std::uniform_int_distribution<uint32_t> seedDistribution(1u, 0xfffffff0u);
    std::uniform_real_distribution<float> speedScale(0.90f, 1.10f);
    std::uniform_real_distribution<float> amplitudeScale(0.75f, 1.30f);
    std::uniform_real_distribution<float> widthScale(0.80f, 1.25f);
    std::uniform_real_distribution<float> forwardScale(0.80f, 1.20f);
    std::uniform_real_distribution<float> curvatureScale(0.65f, 1.35f);
    std::uniform_real_distribution<float> foamScale(0.70f, 1.35f);
    std::uniform_real_distribution<float> phase(0.0f, 1.0f);
    std::uniform_real_distribution<float> angle(0.0f, glm::two_pi<float>());

    BoreEvent event{};
    event.id = m_NextId++;
    event.seed = seedDistribution(m_Random);
    event.progressMeters = 0.0f;
    event.speed = config.baseSpeed * speedScale(m_Random);
    event.age = 0.0f;
    event.amplitudeScale = amplitudeScale(m_Random);
    event.widthScale = widthScale(m_Random);
    event.forwardScale = forwardScale(m_Random);
    event.curvatureScale = curvatureScale(m_Random);
    event.foamScale = foamScale(m_Random);
    event.profilePhaseOffset = phase(m_Random);
    event.variationPhase = angle(m_Random);
    event.active = true;
    return event;
}

void BoreEventManager::ScheduleSpawn(float minSeconds, float maxSeconds)
{
    float safeMax = std::max(minSeconds, maxSeconds);
    std::uniform_real_distribution<float> spawnInterval(minSeconds, safeMax);
    m_SpawnCountdown = spawnInterval(m_Random);
}

const BoreEvent* BoreEventManager::GetNewestActiveEvent() const
{
    const BoreEvent* newest = nullptr;

    for(const BoreEvent& event : m_Events){
        if(!event.active){
            continue;
        }

        if(newest == nullptr || event.id > newest->id){
            newest = &event;
        }
    }

    return newest;
}
}
