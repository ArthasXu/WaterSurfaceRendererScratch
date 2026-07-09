#include "scene/water/sources/WSTessendorfCPU.h"

#include <glm/common.hpp>
#include <glm/geometric.hpp>
#include <glm/gtc/constants.hpp>

#include <algorithm>
#include <cmath>
#include <random>
#include <stdexcept>

namespace water
{
WSTessendorfCPU::WSTessendorfCPU(const TessendorfSpectrumParams& params)
    : m_Params(params),
      m_N(params.resolution)
{
    ValidateParams();

    m_Params.windDirection = glm::normalize(m_Params.windDirection);

    const size_t count = static_cast<size_t>(m_N) * static_cast<size_t>(m_N);

    m_WaveVectors.resize(count);
    m_WaveNumbers.resize(count);
    m_Dispersion.resize(count);
    m_PhillipsValues.resize(count);

    m_H0.resize(count);
    m_H0MinusConj.resize(count);

    m_HeightSpectrum.resize(count);
    m_SlopeXSpectrum.resize(count);
    m_SlopeZSpectrum.resize(count);
    m_DisplacementXSpectrum.resize(count);
    m_DisplacementZSpectrum.resize(count);
    m_DDxdxSpectrum.resize(count);
    m_DDzdzSpectrum.resize(count);
    m_DDxDzSpectrum.resize(count);

    m_Frame.resolution = m_N;
    m_Frame.patchLength = m_Params.patchLength;
    m_Frame.displacement.resize(count);
    m_Frame.normalAux.resize(count);

    ComputeWaveVectors();
    ComputeInitialSpectrum();
    ComputeAtTime(0.0f);
}

void WSTessendorfCPU::ValidateParams() const
{
    if(!IsPowerOfTwo(m_Params.resolution)){
        throw std::runtime_error("Tessendorf resolution must be power of two");
    }

    if(m_Params.resolution < 16){
        throw std::runtime_error("Tessendorf resolution must be >= 16");
    }

    if(m_Params.patchLength <= 0.0f){
        throw std::runtime_error("Tessendorf patchLength must be > 0");
    }

    if(m_Params.windSpeed < 0.0f){
        throw std::runtime_error("Tessendorf windSpeed must be >= 0");
    }

    if(glm::length(m_Params.windDirection) < 1e-6f){
        throw std::runtime_error("Tessendorf windDirection must be non-zero");
    }

    if(m_Params.gravity <= 0.0f){
        throw std::runtime_error("Tessendorf gravity must be > 0");
    }
}

bool WSTessendorfCPU::IsPowerOfTwo(uint32_t value)
{
    return value != 0 && (value & (value - 1)) == 0;
}

int WSTessendorfCPU::SignedFrequencyIndex(uint32_t i, uint32_t n)
{
    return i <= n / 2
        ? static_cast<int>(i)
        : static_cast<int>(i) - static_cast<int>(n);
}

size_t WSTessendorfCPU::Index(uint32_t x, uint32_t z) const
{
    return static_cast<size_t>(z) * static_cast<size_t>(m_N) + static_cast<size_t>(x);
}

void WSTessendorfCPU::ComputeWaveVectors()
{
    for(uint32_t z = 0; z < m_N; z++){
        for(uint32_t x = 0; x < m_N; x++){
            int nx = SignedFrequencyIndex(x, m_N);
            int nz = SignedFrequencyIndex(z, m_N);

            glm::vec2 waveVector{
                glm::two_pi<float>() * static_cast<float>(nx) / m_Params.patchLength,
                glm::two_pi<float>() * static_cast<float>(nz) / m_Params.patchLength
            };

            size_t index = Index(x, z);

            float waveNumber = glm::length(waveVector);

            m_WaveVectors[index] = waveVector;
            m_WaveNumbers[index] = waveNumber;
            m_Dispersion[index] = waveNumber > 1e-6f
                ? std::sqrt(m_Params.gravity * waveNumber)
                : 0.0f;
            m_PhillipsValues[index] = PhillipsSpectrum(waveVector);
        }
    }
}

void WSTessendorfCPU::ComputeInitialSpectrum()
{
    std::mt19937 generator(m_Params.randomSeed);
    std::normal_distribution<float> normalDistribution(0.0f, 1.0f);

    for(uint32_t z = 0; z < m_N; z++){
        for(uint32_t x = 0; x < m_N; x++){
            size_t index = Index(x, z);

            std::complex<float> gaussian{
                normalDistribution(generator),
                normalDistribution(generator)
            };

            float phillips = m_PhillipsValues[index];
            float scale = std::sqrt(phillips * 0.5f);

            m_H0[index] = gaussian * scale;
        }
    }

    for(uint32_t z = 0; z < m_N; z++){
        for(uint32_t x = 0; x < m_N; x++){
            uint32_t minusX = (m_N - x) % m_N;
            uint32_t minusZ = (m_N - z) % m_N;

            m_H0MinusConj[Index(x, z)] =
                std::conj(m_H0[Index(minusX, minusZ)]);
        }
    }
}

float WSTessendorfCPU::PhillipsSpectrum(glm::vec2 waveVector) const
{
    float kLength = glm::length(waveVector);

    if(kLength < 1e-6f){
        return 0.0f;
    }

    // 波矢量与单位方向
    glm::vec2 kHat = waveVector / kLength;
    glm::vec2 windHat = glm::normalize(m_Params.windDirection);

    // 最大波长与阻尼长度
    float windSpeed = std::max(m_Params.windSpeed, 1e-6f);
    float largestWave = windSpeed * windSpeed / m_Params.gravity; // L_max = V²/g

    float dampingLength = largestWave * m_Params.shortWaveDamping; // 短波阻尼尺度

    float kLengthSquared = kLength * kLength;
    float kLengthFourth = kLengthSquared * kLengthSquared;

    float directional = glm::dot(kHat, windHat);
    float directionalPower = directional * directional;

    if(directional < 0.0f){
        directionalPower *= m_Params.oppositeWindDamping;
    }

    // 长波抑制
    float longWaveSuppress =
        std::exp(-1.0f / (kLengthSquared * largestWave * largestWave));

    // 短波抑制
    float shortWaveSuppress =
        std::exp(-kLengthSquared * dampingLength * dampingLength);

    // 组合频谱
        float spectrum =
        m_Params.spectrumAmplitude *
        longWaveSuppress *
        directionalPower *
        shortWaveSuppress /
        kLengthFourth;

    if(!std::isfinite(spectrum)){
        return 0.0f;
    }

    return std::max(spectrum, 0.0f);
}

void WSTessendorfCPU::Update(float deltaTime)
{
    m_Time += deltaTime;
    ComputeAtTime(m_Time);
}

void WSTessendorfCPU::ComputeAtTime(float timeSeconds)
{
    m_Time = timeSeconds;

    ComputeSpectrumAtTime(timeSeconds);
    ExecuteInverseFFTs();
    AssembleSpatialFrame();
}

void WSTessendorfCPU::ComputeSpectrumAtTime(float timeSeconds)
{
    (void)timeSeconds;

    std::fill(m_HeightSpectrum.begin(), m_HeightSpectrum.end(), std::complex<float>{0.0f, 0.0f});
    // 从 m_HeightSpectrum.begin() 开始，到 m_HeightSpectrum.end() 结束（即整个向量）。
    // 将 m_HeightSpectrum 中的每一个元素都设置为 std::complex<float>{0.0f, 0.0f}，也就是复数 0 + 0i
    std::fill(m_SlopeXSpectrum.begin(), m_SlopeXSpectrum.end(), std::complex<float>{0.0f, 0.0f});
    std::fill(m_SlopeZSpectrum.begin(), m_SlopeZSpectrum.end(), std::complex<float>{0.0f, 0.0f});
    std::fill(m_DisplacementXSpectrum.begin(), m_DisplacementXSpectrum.end(), std::complex<float>{0.0f, 0.0f});
    std::fill(m_DisplacementZSpectrum.begin(), m_DisplacementZSpectrum.end(), std::complex<float>{0.0f, 0.0f});
    std::fill(m_DDxdxSpectrum.begin(), m_DDxdxSpectrum.end(), std::complex<float>{0.0f, 0.0f});
    std::fill(m_DDzdzSpectrum.begin(), m_DDzdzSpectrum.end(), std::complex<float>{0.0f, 0.0f});
    std::fill(m_DDxDzSpectrum.begin(), m_DDxDzSpectrum.end(), std::complex<float>{0.0f, 0.0f});
}

void WSTessendorfCPU::ExecuteInverseFFTs()
{
}

void WSTessendorfCPU::AssembleSpatialFrame()
{
    for(uint32_t z = 0; z < m_N; z++){
        for(uint32_t x = 0; x < m_N; x++){
            size_t index = Index(x, z);

            m_Frame.displacement[index] = glm::vec4(0.0f);
            m_Frame.normalAux[index] = glm::vec4(0.0f);
        }
    }
}

WaterSurfaceSample WSTessendorfCPU::Sample(glm::vec2 worldXZ) const
{
    (void)worldXZ;

    WaterSurfaceSample sample{};
    return sample;
}

const CPUWaterSurfaceFrame& WSTessendorfCPU::GetFrame() const
{
    return m_Frame;
}

uint32_t WSTessendorfCPU::GetResolution() const
{
    return m_N;
}

float WSTessendorfCPU::GetPatchLength() const
{
    return m_Params.patchLength;
}

glm::vec2 WSTessendorfCPU::GetWaveVector(uint32_t x, uint32_t z) const
{
    return m_WaveVectors[Index(x, z)];
}

float WSTessendorfCPU::GetWaveNumber(uint32_t x, uint32_t z) const
{
    return m_WaveNumbers[Index(x, z)];
}

float WSTessendorfCPU::GetPhillipsValue(uint32_t x, uint32_t z) const
{
    return m_PhillipsValues[Index(x, z)];
}

float WSTessendorfCPU::ComputeH0Checksum(uint32_t maxCount) const
{
    uint32_t count = std::min<uint32_t>(
        maxCount,
        static_cast<uint32_t>(m_H0.size())
    );

    float checksum = 0.0f;

    for(uint32_t i = 0; i < count; i++){
        checksum += m_H0[i].real() * 17.0f;
        checksum += m_H0[i].imag() * 31.0f;
    }

    return checksum;
}
}