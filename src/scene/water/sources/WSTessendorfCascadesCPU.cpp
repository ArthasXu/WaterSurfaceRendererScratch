#include "scene/water/sources/WSTessendorfCascadesCPU.h"

#include <glm/common.hpp>
#include <glm/gtc/constants.hpp>

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace
{
float SmoothStep(float edge0, float edge1, float x)
{
    if(std::abs(edge1 - edge0) < 1e-6f){
        return x >= edge1 ? 1.0f : 0.0f;
    }

    float t = (x - edge0) / (edge1 - edge0);
    t = std::clamp(t, 0.0f, 1.0f);

    return t * t * (3.0f - 2.0f * t);
}

float KFromWavelength(float wavelength)
{
    return glm::two_pi<float>() / wavelength;
}
}

namespace water
{
// 长波 / 低频层 >256m 波长从 256m 逐渐到 128m 时，这一层的权重从 1 平滑降为 0
SpectrumBand WSTessendorfCascadesCPU::MakeLongBand()
{
    SpectrumBand band{};
    band.fadeInStart = 0.0f;
    band.fadeInEnd = 0.0f;
    band.fadeOutStart = KFromWavelength(256.0f);
    band.fadeOutEnd = KFromWavelength(128.0f);
    return band;
}

// 中波 / 中低频层 48~128m 波长从 256m 到 128m 时，权重从 0 平滑升为 1 48m ~ 24m，这一层的权重从 1 平滑到 0
SpectrumBand WSTessendorfCascadesCPU::MakeMidBand()
{
    SpectrumBand band{};
    band.fadeInStart = KFromWavelength(256.0f);
    band.fadeInEnd = KFromWavelength(128.0f);
    band.fadeOutStart = KFromWavelength(48.0f);
    band.fadeOutEnd = KFromWavelength(24.0f);
    return band;
}

// 短波 / 高频层 <24m 波长从 48m ~ 24m，这一层的权重从 0 ~ 1
SpectrumBand WSTessendorfCascadesCPU::MakeShortBand()
{
    SpectrumBand band{};
    band.fadeInStart = KFromWavelength(48.0f);
    band.fadeInEnd = KFromWavelength(24.0f);
    return band;
}

float WSTessendorfCascadesCPU::LongWeight(float waveNumber)
{
    float longToMid = SmoothStep(
        KFromWavelength(256.0f),
        KFromWavelength(128.0f),
        waveNumber
    );

    return 1.0f - longToMid;
}

float WSTessendorfCascadesCPU::MidWeight(float waveNumber)
{
    float longToMid = SmoothStep(
        KFromWavelength(256.0f),
        KFromWavelength(128.0f),
        waveNumber
    );

    float midToShort = SmoothStep(
        KFromWavelength(48.0f),
        KFromWavelength(24.0f),
        waveNumber
    );

    return longToMid * (1.0f - midToShort);
}

float WSTessendorfCascadesCPU::ShortWeight(float waveNumber)
{
    float midToShort = SmoothStep(
        KFromWavelength(48.0f),
        KFromWavelength(24.0f),
        waveNumber
    );

    return midToShort;
}

float WSTessendorfCascadesCPU::WeightSum(float waveNumber)
{
    return
        LongWeight(waveNumber) +
        MidWeight(waveNumber) +
        ShortWeight(waveNumber);
}

WSTessendorfCascadesCPU::WSTessendorfCascadesCPU(const MultiCascadeParams& params)
{
    TessendorfSpectrumParams shortParams = params.baseSpectrum;
    shortParams.resolution = params.resolution;
    shortParams.patchLength = params.shortPatchLength;
    shortParams.randomSeed = params.baseSeed + 0;
    shortParams.spectrumBand = MakeShortBand();

    TessendorfSpectrumParams midParams = params.baseSpectrum;
    midParams.resolution = params.resolution;
    midParams.patchLength = params.midPatchLength;
    midParams.randomSeed = params.baseSeed + 101;
    midParams.spectrumBand = MakeMidBand();

    TessendorfSpectrumParams longParams = params.baseSpectrum;
    longParams.resolution = params.resolution;
    longParams.patchLength = params.longPatchLength;
    longParams.randomSeed = params.baseSeed + 211;
    longParams.spectrumBand = MakeLongBand();

    m_Cascades[0] = std::make_unique<WSTessendorfCPU>(shortParams);
    m_Cascades[1] = std::make_unique<WSTessendorfCPU>(midParams);
    m_Cascades[2] = std::make_unique<WSTessendorfCPU>(longParams);
}

void WSTessendorfCascadesCPU::Update(float deltaTime)
{
    for(std::unique_ptr<WSTessendorfCPU>& cascade : m_Cascades){
        cascade->Update(deltaTime);
    }
}

void WSTessendorfCascadesCPU::ComputeAtTime(float timeSeconds)
{
    for(std::unique_ptr<WSTessendorfCPU>& cascade : m_Cascades){
        cascade->ComputeAtTime(timeSeconds);
    }
}

WaterSurfaceSample WSTessendorfCascadesCPU::Sample(glm::vec2 worldXZ) const
{
    WaterSurfaceSample result{};

    for(const std::unique_ptr<WSTessendorfCPU>& cascade : m_Cascades){
        WaterSurfaceSample sample = cascade->Sample(worldXZ);

        result.height += sample.height;
        result.horizontalDisplacement += sample.horizontalDisplacement;
        result.slope += sample.slope;
        result.velocity += sample.velocity;
        result.foamSource = std::max(result.foamSource, sample.foamSource);
        result.blendMask *= sample.blendMask;
    }

    return result;
}

const WSTessendorfCPU& WSTessendorfCascadesCPU::GetCascade(uint32_t index) const
{
    if(index >= m_Cascades.size()){
        throw std::runtime_error("Cascade index out of range");
    }

    return *m_Cascades[index];
}
}