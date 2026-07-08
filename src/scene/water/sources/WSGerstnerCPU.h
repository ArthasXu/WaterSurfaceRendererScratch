#pragma once

#include "scene/water/sources/ICPUWaterSurfaceSource.h"

#include <glm/glm.hpp>

#include <vector>

namespace water
{
struct GerstnerWave
{
    glm::vec2 direction{1.0f, 0.0f};

    float amplitude = 1.0f;
    float wavelength = 20.0f;

    float phaseSpeed = 5.0f;

    float steepness = 0.4f;
    float phaseOffset = 0.0f;
}; // 波的参数

class WSGerstnerCPU final : public ICPUWaterSurfaceSource
{
public:
    explicit WSGerstnerCPU(std::vector<GerstnerWave> waves); // 构造函数，接受波的参数

    void Update(float deltaTime) override;

    WaterSurfaceSample Sample(glm::vec2 worldXZ) const override;

    void ResetTime();

    float GetTime() const;

private:
    std::vector<GerstnerWave> m_Waves; // 波的参数
    float m_Time = 0.0f; // 时间
};

}