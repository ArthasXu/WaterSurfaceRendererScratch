#include "scene/water/sources/WSTessendorfCPU.h"

#include <fftw3.h>

#include <glm/common.hpp>
#include <glm/geometric.hpp>
#include <glm/gtc/constants.hpp>

#include <algorithm>
#include <cmath>
#include <complex>
#include <limits>
#include <random>
#include <stdexcept>
#include <chrono>

namespace
{
constexpr float kEpsilon = 1e-6f;

std::vector<std::complex<float>> InverseDFT2DNaive(
    const std::vector<std::complex<float>>& spectrum,
    uint32_t resolution
){
    const uint32_t n = resolution;
    const size_t count = static_cast<size_t>(n) * static_cast<size_t>(n);

    if(spectrum.size() != count){
        throw std::runtime_error("InverseDFT2DNaive size mismatch");
    }

    std::vector<std::complex<float>> output(count);

    const std::complex<float> imaginaryUnit{0.0f, 1.0f};
    const float twoPi = glm::two_pi<float>();
    const float normalization = 1.0f / static_cast<float>(count);

    for(uint32_t z = 0; z < n; z++){
        for(uint32_t x = 0; x < n; x++){
            std::complex<float> sum{0.0f, 0.0f};

            for(uint32_t nz = 0; nz < n; nz++){
                for(uint32_t nx = 0; nx < n; nx++){
                    float phase =
                        twoPi *
                        (
                            static_cast<float>(nx * x) / static_cast<float>(n) +
                            static_cast<float>(nz * z) / static_cast<float>(n)
                        );

                    std::complex<float> exponent =
                        std::exp(imaginaryUnit * phase);

                    size_t spectrumIndex =
                        static_cast<size_t>(nz) * static_cast<size_t>(n) +
                        static_cast<size_t>(nx);

                    sum += spectrum[spectrumIndex] * exponent;
                }
            }

            size_t outputIndex =
                static_cast<size_t>(z) * static_cast<size_t>(n) +
                static_cast<size_t>(x);

            output[outputIndex] = sum * normalization;
        }
    }

    return output;
}

float ComplexAbs(std::complex<float> value)
{
    return std::sqrt(value.real() * value.real() + value.imag() * value.imag());
}

glm::vec4 Lerp(glm::vec4 a, glm::vec4 b, float t)
{
    return a * (1.0f - t) + b * t;
}

float SmoothStep(float edge0, float edge1, float x)
{   // 平滑频带过渡，避免硬切导致 ringing
    if(!std::isfinite(edge0) || !std::isfinite(edge1)){
        return x >= edge0 ? 1.0f : 0.0f;
    }

    if(std::abs(edge1 - edge0) < 1e-6f){
        return x >= edge1 ? 1.0f : 0.0f;
    }

    float t = (x - edge0) / (edge1 - edge0);
    t = std::clamp(t, 0.0f, 1.0f);

    return t * t * (3.0f - 2.0f * t);
}

}


namespace water
{
class FFTWorkspace2D
{
public:
    explicit FFTWorkspace2D(uint32_t resolution)
        : m_N(resolution)
    {
        const size_t count =
            static_cast<size_t>(m_N) * static_cast<size_t>(m_N);

        m_Input = reinterpret_cast<fftw_complex*>(
            fftw_malloc(sizeof(fftw_complex) * count)
        );

        m_Output = reinterpret_cast<fftw_complex*>(
            fftw_malloc(sizeof(fftw_complex) * count)
        );

        if(m_Input == nullptr || m_Output == nullptr){
            throw std::runtime_error("Failed to allocate FFTW workspace memory");
        }

        m_Plan = fftw_plan_dft_2d(
            static_cast<int>(m_N),
            static_cast<int>(m_N),
            m_Input,
            m_Output,
            FFTW_BACKWARD,
            FFTW_ESTIMATE
        );

        if(m_Plan == nullptr){
            throw std::runtime_error("Failed to create FFTW workspace plan");
        }
    }

    ~FFTWorkspace2D()
    {
        if(m_Plan != nullptr){
            fftw_destroy_plan(m_Plan);
        }

        if(m_Input != nullptr){
            fftw_free(m_Input);
        }

        if(m_Output != nullptr){
            fftw_free(m_Output);
        }
    }

    FFTWorkspace2D(const FFTWorkspace2D&) = delete;
    FFTWorkspace2D& operator=(const FFTWorkspace2D&) = delete;

    void ExecuteInverse(
        const std::vector<std::complex<float>>& spectrum,
        std::vector<std::complex<float>>& spatial
    ){
        const size_t count =
            static_cast<size_t>(m_N) * static_cast<size_t>(m_N);

        if(spectrum.size() != count || spatial.size() != count){
            throw std::runtime_error("FFTWorkspace2D::ExecuteInverse size mismatch");
        }

        for(size_t i = 0; i < count; i++){
            m_Input[i][0] = spectrum[i].real();
            m_Input[i][1] = spectrum[i].imag();
        }

        fftw_execute(m_Plan);

        const double normalization =
            1.0 / static_cast<double>(count);

        for(size_t i = 0; i < count; i++){
            spatial[i] = {
                static_cast<float>(m_Output[i][0] * normalization),
                static_cast<float>(m_Output[i][1] * normalization)
            };
        }
    }

private:
    uint32_t m_N = 0;

    fftw_complex* m_Input = nullptr;
    fftw_complex* m_Output = nullptr;

    fftw_plan m_Plan = nullptr;
};

WSTessendorfCPU::~WSTessendorfCPU() = default;

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

    m_BandWeights.resize(count);

    m_H0.resize(count);
    m_H0MinusConjugate.resize(count);

    m_HeightSpectrum.resize(count);
    m_SlopeXSpectrum.resize(count);
    m_SlopeZSpectrum.resize(count);
    m_DisplacementXSpectrum.resize(count);
    m_DisplacementZSpectrum.resize(count);
    m_DDxdxSpectrum.resize(count);
    m_DDzdzSpectrum.resize(count);
    m_DDxdzSpectrum.resize(count);

    m_HeightSpatial.resize(count);
    m_SlopeXSpatial.resize(count);
    m_SlopeZSpatial.resize(count);
    m_DisplacementXSpatial.resize(count);
    m_DisplacementZSpatial.resize(count);
    m_DDxdxSpatial.resize(count);
    m_DDzdzSpatial.resize(count);
    m_DDxdzSpatial.resize(count);

    m_FFTWorkspace = std::make_unique<FFTWorkspace2D>(m_N); // 创建 FFT 工作区，用于执行 FFT 和 IFFT 操作

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
            m_BandWeights[index] = BandWeight(waveNumber);
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

            m_H0MinusConjugate[Index(x, z)] =
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

    float globalPhillips = std::max(spectrum, 0.0f);
    return globalPhillips * BandWeight(kLength);
}

float WSTessendorfCPU::BandWeight(float waveNumber) const
{   
    const SpectrumBand& band = m_Params.spectrumBand; // 频谱带宽参数

    float lowWeight = 1.0f;
    float highWeight = 1.0f;

    // waveNumber 很小 波长很长）：这个波应该主要由更低频（更粗糙）的 FFT 层来负责。
    // 当前层的权重 lowWeight 接近 0，它不会生成这个长波
    if(band.fadeInEnd > band.fadeInStart){ // 淡入区间
        lowWeight = SmoothStep(
            band.fadeInStart,
            band.fadeInEnd,
            waveNumber
        );
    }

    // waveNumber 很大（波长很短）：这个波应该主要由更高频（更精细）的 FFT 层来负责。
    // 当前层的权重 highWeight 接近 0，它不会生成这个短波
    if(std::isfinite(band.fadeOutStart) &&
        std::isfinite(band.fadeOutEnd) &&
        band.fadeOutEnd > band.fadeOutStart){ // 淡出区间
        highWeight =
            1.0f -
            SmoothStep(
                band.fadeOutStart,
                band.fadeOutEnd,
                waveNumber
            );
    }

    float kMin = GetRepresentableMinWaveNumber();
    float kMax = GetRepresentableMaxWaveNumber();

    if(waveNumber > 0.0f && waveNumber < kMin){
        return 0.0f;
    }

    if(waveNumber > kMax){
        return 0.0f;
    }

    // 相乘而非取min是为了控制频段的低端和高端的平滑衰减
    return glm::clamp(lowWeight * highWeight, 0.0f, 1.0f);

}

void WSTessendorfCPU::Update(float deltaTime)
{
    m_Time += deltaTime;
    ComputeAtTime(m_Time);
}

void WSTessendorfCPU::ComputeAtTime(float timeSeconds)
{
    using Clock = std::chrono::high_resolution_clock;

    m_Time = timeSeconds;

    auto totalStart = Clock::now();

    auto spectrumStart = Clock::now();
    ComputeSpectrumAtTime(timeSeconds);
    auto spectrumEnd = Clock::now();

    auto ifftStart = Clock::now();
    ExecuteInverseFFTs();
    auto ifftEnd = Clock::now();

    auto assemblyStart = Clock::now();
    AssembleSpatialFrame();
    auto assemblyEnd = Clock::now();

    auto totalEnd = Clock::now();

    m_LastTimingStats.spectrumMilliseconds =
        std::chrono::duration<double, std::milli>(
            spectrumEnd - spectrumStart
        ).count();

    m_LastTimingStats.ifftMilliseconds =
        std::chrono::duration<double, std::milli>(
            ifftEnd - ifftStart
        ).count();

    m_LastTimingStats.assemblyMilliseconds =
        std::chrono::duration<double, std::milli>(
            assemblyEnd - assemblyStart
        ).count();

    m_LastTimingStats.totalMilliseconds =
        std::chrono::duration<double, std::milli>(
            totalEnd - totalStart
        ).count();
}

void WSTessendorfCPU::ComputeSpectrumAtTime(float timeSeconds)
{
    const std::complex<float> imaginaryUnit{0.0f, 1.0f}; // i
    
    m_LastHermitianMaxError = 0.0f;

    for(uint32_t z = 0; z < m_N; z++){
        for(uint32_t x = 0; x < m_N; x++){
            size_t index = Index(x, z); // 计算索引

            glm::vec2 k = m_WaveVectors[index]; // 波矢量
            float kLength = glm::length(k); // 波矢量长度
            float omega = m_Dispersion[index]; // 波速

            std::complex<float> positivePhase = std::exp(imaginaryUnit * omega * timeSeconds); // 正相位因子
            std::complex<float> negativePhase = std::exp(-imaginaryUnit * omega * timeSeconds); // 负相位因子
            std::complex<float> h = m_H0[index] * positivePhase + m_H0MinusConjugate[index] * negativePhase; // 复振幅

            m_HeightSpectrum[index] = h; // 高度频谱
            m_SlopeXSpectrum[index] =
                imaginaryUnit * k.x * h; // x方向斜率频谱
            m_SlopeZSpectrum[index] =
                imaginaryUnit * k.y * h; // z方向斜率频谱

            if(kLength > kEpsilon){ // 避免除以零
                m_DisplacementXSpectrum[index] =
                    -imaginaryUnit * (k.x / kLength) * h; // x方向位移频谱

                m_DisplacementZSpectrum[index] =
                    -imaginaryUnit * (k.y / kLength) * h; // z方向位移频谱

                // Jacobian 在这里指的是水面水平位移场（Displacement）对空间坐标的偏导数矩阵
                m_DDxdxSpectrum[index] =
                    (k.x * k.x / kLength) * h; // x方向位移Jacobian项

                m_DDzdzSpectrum[index] =
                    (k.y * k.y / kLength) * h; // z方向位移Jacobian项

                m_DDxdzSpectrum[index] =
                    (k.x * k.y / kLength) * h; // xz方向位移Jacobian项
            }
            else{
                m_DisplacementXSpectrum[index] = {0.0f, 0.0f};
                m_DisplacementZSpectrum[index] = {0.0f, 0.0f};
                m_DDxdxSpectrum[index] = {0.0f, 0.0f};
                m_DDzdzSpectrum[index] = {0.0f, 0.0f};
                m_DDxdzSpectrum[index] = {0.0f, 0.0f};
            }
        }
    }

    for(uint32_t z = 0; z < m_N; z++){
        for(uint32_t x = 0; x < m_N; x++){
            uint32_t minusX = (m_N - x) % m_N;
            uint32_t minusZ = (m_N - z) % m_N;

            std::complex<float> h = m_HeightSpectrum[Index(x, z)];
            std::complex<float> hMinus = m_HeightSpectrum[Index(minusX, minusZ)];

            float error = ComplexAbs(hMinus - std::conj(h));

            m_LastHermitianMaxError =
                std::max(m_LastHermitianMaxError, error);
        }
    }
}

void WSTessendorfCPU::ExecuteInverseFFTs()
{
    m_FFTWorkspace->ExecuteInverse(
        m_HeightSpectrum,
        m_HeightSpatial
    );

    m_FFTWorkspace->ExecuteInverse(
        m_SlopeXSpectrum,
        m_SlopeXSpatial
    );

    m_FFTWorkspace->ExecuteInverse(
        m_SlopeZSpectrum,
        m_SlopeZSpatial
    );

    m_FFTWorkspace->ExecuteInverse(
        m_DisplacementXSpectrum,
        m_DisplacementXSpatial
    );

    m_FFTWorkspace->ExecuteInverse(
        m_DisplacementZSpectrum,
        m_DisplacementZSpatial
    );

    m_FFTWorkspace->ExecuteInverse(
        m_DDxdxSpectrum,
        m_DDxdxSpatial
    );

    m_FFTWorkspace->ExecuteInverse(
        m_DDzdzSpectrum,
        m_DDzdzSpatial
    );

    m_FFTWorkspace->ExecuteInverse(
        m_DDxdzSpectrum,
        m_DDxdzSpatial
    );

    const size_t count =
        static_cast<size_t>(m_N) * static_cast<size_t>(m_N);

    m_LastMaxImaginaryResidual = 0.0f;

    for(size_t i = 0; i < count; i++){
        m_LastMaxImaginaryResidual = std::max(
            m_LastMaxImaginaryResidual,
            std::abs(m_HeightSpatial[i].imag())
        );
    }
}

void WSTessendorfCPU::AssembleSpatialFrame()
{
    const float lambda = m_Params.choppyLambda;

    for(uint32_t z = 0; z < m_N; z++){
        for(uint32_t x = 0; x < m_N; x++){
            size_t index = Index(x, z);

            float height = m_HeightSpatial[index].real();

            float slopeX = m_SlopeXSpatial[index].real();
            float slopeZ = m_SlopeZSpatial[index].real();

            float displacementX = m_DisplacementXSpatial[index].real();
            float displacementZ = m_DisplacementZSpatial[index].real();

            float dDxdx = m_DDxdxSpatial[index].real();
            float dDzdz = m_DDzdzSpatial[index].real();
            float dDxdz = m_DDxdzSpatial[index].real();

            float jxx = 1.0f + lambda * dDxdx;
            float jzz = 1.0f + lambda * dDzdz;
            float jxz = lambda * dDxdz;

            float jacobian = jxx * jzz - jxz * jxz;

            m_Frame.displacement[index] = {
                lambda * displacementX,
                height,
                lambda * displacementZ,
                jacobian
            };

            m_Frame.normalAux[index] = {
                slopeX,
                slopeZ,
                dDxdx,
                dDzdz
            };
        }
    }
}

WaterSurfaceSample WSTessendorfCPU::Sample(glm::vec2 worldXZ) const
{
    float u = worldXZ.x / m_Params.patchLength;
    float v = worldXZ.y / m_Params.patchLength;

    u -= std::floor(u); // 向下取整
    v -= std::floor(v);

    float fx = u * static_cast<float>(m_N);
    float fz = v * static_cast<float>(m_N);

    uint32_t x0 = static_cast<uint32_t>(std::floor(fx)) % m_N;
    uint32_t z0 = static_cast<uint32_t>(std::floor(fz)) % m_N;

    uint32_t x1 = (x0 + 1) % m_N;
    uint32_t z1 = (z0 + 1) % m_N;

    float tx = fx - std::floor(fx);
    float tz = fz - std::floor(fz);

    glm::vec4 d00 = m_Frame.displacement[Index(x0, z0)];
    glm::vec4 d10 = m_Frame.displacement[Index(x1, z0)];
    glm::vec4 d01 = m_Frame.displacement[Index(x0, z1)];
    glm::vec4 d11 = m_Frame.displacement[Index(x1, z1)];

    glm::vec4 n00 = m_Frame.normalAux[Index(x0, z0)];
    glm::vec4 n10 = m_Frame.normalAux[Index(x1, z0)];
    glm::vec4 n01 = m_Frame.normalAux[Index(x0, z1)];
    glm::vec4 n11 = m_Frame.normalAux[Index(x1, z1)];

    glm::vec4 d0 = Lerp(d00, d10, tx);
    glm::vec4 d1 = Lerp(d01, d11, tx);
    glm::vec4 displacement = Lerp(d0, d1, tz);

    glm::vec4 n0 = Lerp(n00, n10, tx);
    glm::vec4 n1 = Lerp(n01, n11, tx);
    glm::vec4 normalAux = Lerp(n0, n1, tz);

    WaterSurfaceSample sample{};
    sample.horizontalDisplacement = {displacement.x, displacement.z};
    sample.height = displacement.y;
    sample.slope = {normalAux.x, normalAux.y};
    sample.foamSource = std::max(0.0f, 1.0f - displacement.w);
    sample.velocity = {0.0f, 0.0f};
    sample.blendMask = 1.0f;

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

float WSTessendorfCPU::GetBandWeight(uint32_t x, uint32_t z) const
{
    return m_BandWeights[Index(x, z)];
}

float WSTessendorfCPU::GetRepresentableMinWaveNumber() const
{
    return glm::two_pi<float>() / m_Params.patchLength;
}

float WSTessendorfCPU::GetRepresentableMaxWaveNumber() const
{
    return glm::pi<float>() * static_cast<float>(m_N) / m_Params.patchLength;
}

std::complex<float> WSTessendorfCPU::GetH0(uint32_t x, uint32_t z) const
{
    return m_H0[Index(x, z)];
}

const std::vector<std::complex<float>>& WSTessendorfCPU::GetH0Field() const
{
    return m_H0;
}

const std::vector<std::complex<float>>& WSTessendorfCPU::GetH0MinusConjugateField() const
{
    return m_H0MinusConjugate;
}

const std::vector<glm::vec2>& WSTessendorfCPU::GetWaveVectors() const
{
    return m_WaveVectors;
}

const std::vector<float>& WSTessendorfCPU::GetDispersionField() const
{
    return m_Dispersion;
}

std::complex<float> WSTessendorfCPU::GetH0MinusConjugate(uint32_t x, uint32_t z) const
{
    return m_H0MinusConjugate[Index(x, z)];
}

std::complex<float> WSTessendorfCPU::GetHeightSpectrum(uint32_t x, uint32_t z) const
{
    return m_HeightSpectrum[Index(x, z)];
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

float WSTessendorfCPU::ComputeFrameChecksum() const
{
    float checksum = 0.0f;

    for(size_t i = 0; i < m_Frame.displacement.size(); i++){
        checksum += m_Frame.displacement[i].x * 3.0f;
        checksum += m_Frame.displacement[i].y * 5.0f;
        checksum += m_Frame.displacement[i].z * 7.0f;
        checksum += m_Frame.displacement[i].w * 11.0f;
    }

    return checksum;
}

float WSTessendorfCPU::GetLastHermitianMaxError() const
{
    return m_LastHermitianMaxError;
}

float WSTessendorfCPU::GetLastMaxImaginaryResidual() const
{
    return m_LastMaxImaginaryResidual;
}

FFTValidationStats WSTessendorfCPU::ValidateNaiveAgainstFFTW(float timeSeconds)
{
    ComputeSpectrumAtTime(timeSeconds);

    std::vector<std::complex<float>> naive =
        InverseDFT2DNaive(m_HeightSpectrum, m_N);

    std::vector<std::complex<float>> fftwSpatial(
        static_cast<size_t>(m_N) * static_cast<size_t>(m_N)
    );

    m_FFTWorkspace->ExecuteInverse(
        m_HeightSpectrum,
        fftwSpatial
    );

    const size_t count = static_cast<size_t>(m_N) * static_cast<size_t>(m_N);

    FFTValidationStats stats{};

    double sumAbsError = 0.0;
    double sumSquaredError = 0.0;
    double sumSquaredReference = 0.0;

    for(size_t i = 0; i < count; i++){
        std::complex<float> fftwValue = fftwSpatial[i];

        float realError =
            std::abs(naive[i].real() - fftwValue.real());

        stats.maxAbsRealError =
            std::max(stats.maxAbsRealError, realError);

        sumAbsError += realError;
        sumSquaredError += static_cast<double>(realError) * static_cast<double>(realError);
        sumSquaredReference +=
            static_cast<double>(naive[i].real()) *
            static_cast<double>(naive[i].real());

        stats.maxImaginaryResidual =
            std::max(stats.maxImaginaryResidual, std::abs(fftwValue.imag()));
    }

    stats.meanAbsRealError =
        static_cast<float>(sumAbsError / static_cast<double>(count));

    if(sumSquaredReference > 1e-20){
        stats.relativeRmsError =
            static_cast<float>(std::sqrt(sumSquaredError / sumSquaredReference));
    }
    else{
        stats.relativeRmsError = 0.0f;
    }

    ComputeAtTime(timeSeconds);

    return stats;
}

const TessendorfTimingStats& WSTessendorfCPU::GetLastTimingStats() const
{
    return m_LastTimingStats;
}

}