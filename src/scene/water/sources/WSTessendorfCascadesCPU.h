#pragma once

#include "scene/water/sources/ICPUWaterSurfaceSource.h"
#include "scene/water/sources/WSTessendorfCPU.h"

#include <array>
#include <memory>

namespace water
{
struct MultiCascadeParams
{
    TessendorfSpectrumParams baseSpectrum{};

    uint32_t resolution = 128;
    uint32_t baseSeed = 1337;

    float shortPatchLength = 64.0f;
    float midPatchLength = 256.0f;
    float longPatchLength = 1024.0f;
};

class WSTessendorfCascadesCPU final : public ICPUWaterSurfaceSource
{
public:
    explicit WSTessendorfCascadesCPU(const MultiCascadeParams& params);

    void Update(float deltaTime) override;

    void ComputeAtTime(float timeSeconds);

    WaterSurfaceSample Sample(glm::vec2 worldXZ) const override;

    const WSTessendorfCPU& GetCascade(uint32_t index) const;

    static SpectrumBand MakeLongBand();
    static SpectrumBand MakeMidBand();
    static SpectrumBand MakeShortBand();

    static float LongWeight(float waveNumber);
    static float MidWeight(float waveNumber);
    static float ShortWeight(float waveNumber);
    static float WeightSum(float waveNumber);

private:
    std::array<std::unique_ptr<WSTessendorfCPU>, 3> m_Cascades;
};
}