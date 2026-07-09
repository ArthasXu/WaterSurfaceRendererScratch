#pragma once

#include "scene/water/sources/ICPUWaterSurfaceSource.h"

#include <glm/glm.hpp>

#include <complex>
#include <cstdint>
#include <vector>

namespace water
{
struct TessendorfSpectrumParams
{
    uint32_t resolution = 64;               // 频域网格的尺寸 决定了你能模拟多少个不同的波矢量 64*64 个
    // 分辨率越高，海面的细节越丰富，但 FFT 的计算量也越大 N^2（logN) 级别，N 为 resolution
    float patchLength = 256.0f;             // 海洋补丁在现实世界中的边长 

    glm::vec2 windDirection{1.0f, 0.0f};    // 二维归一化向量，表示风在海平面上吹动的方向
    float windSpeed = 25.0f;                // 风速，单位为米/秒

    float spectrumAmplitude = 0.0005f;      // 一个全局乘数，用于缩放整个 Phillips 频谱的振幅
    float shortWaveDamping = 0.001f;        // 短波阻尼 抑制高频短波的能力

    float gravity = 9.81f;                  // 重力加速度 用于色散关系
    float choppyLambda = 1.0f;              // 水平位移 Lambda 系数 值越大波峰越尖锐

    float oppositeWindDamping = 0.07f;      // 与风向相反的风的阻尼系数 进一步压制逆风方向的波

    uint32_t randomSeed = 1337;             // Tessendorf 方法需要为每个波矢量生成随机复数振幅 
};

struct FFTValidationStats
{
    float maxAbsRealError = 0.0f;
    float meanAbsRealError = 0.0f;
    float relativeRmsError = 0.0f;
    float maxImaginaryResidual = 0.0f;
};

class WSTessendorfCPU final : public ICPUWaterSurfaceSource
{
public:
    explicit WSTessendorfCPU(const TessendorfSpectrumParams& params); // 用频谱参数初始化波浪模拟器，构建波矢量、计算初始频谱

    void Update(float deltaTime) override;          // 每帧更新时间，并调用 ComputeAtTime 重算当前频谱和空间域数据
    void ComputeAtTime(float timeSeconds);          // 根据给定时间完成：频谱演化 → 逆 FFT → 组装空间域高度/位移/法线

    WaterSurfaceSample Sample(glm::vec2 worldXZ) const override; // 对世界空间中的点进行水面采样，返回高度、水平位移、斜率

    const CPUWaterSurfaceFrame& GetFrame() const;   // 获取当前帧的完整空间域水面数据（高度场、位移场、法线场等）
    uint32_t GetResolution() const;                 // 返回频谱网格分辨率（N = resolution）
    float GetPatchLength() const;                   // 返回海面补丁的物理边长（米）

    glm::vec2 GetWaveVector(uint32_t x, uint32_t z) const; // 获取指定频域格点的波矢量 k = (kx, kz)
    float GetWaveNumber(uint32_t x, uint32_t z) const;     // 获取指定格点的波数 k = |k|
    float GetPhillipsValue(uint32_t x, uint32_t z) const;  // 获取指定格点的 Phillips 频谱值（能量分布）

    std::complex<float> GetH0(uint32_t x, uint32_t z) const; // 获取指定格点的初始复振幅 h0(k)
    std::complex<float> GetH0MinusConjugate(uint32_t x, uint32_t z) const; // 获取 h0*(-k)，用于频谱对称性
    std::complex<float> GetHeightSpectrum(uint32_t x, uint32_t z) const; // 获取高度频谱 ̃h(k,t)
    
    float ComputeH0Checksum(uint32_t maxCount = 16) const; // 计算前 maxCount 个 h0 的校验和，用于验证随机振幅的可重现性
    float ComputeFrameChecksum() const; // 计算当前帧的校验和，用于验证模拟结果的一致性

    float GetLastHermitianMaxError() const; // 获取最后一次逆 FFT 后的最大 Hermitian 误差
    float GetLastMaxImaginaryResidual() const; // 获取最后一次逆 FFT 后的最大虚部残差

    FFTValidationStats ValidateNaiveAgainstFFTW(float timeSeconds); // 使用 FFTW 验证模拟结果的正确性


private:
    void ValidateParams() const;       // 校验频谱参数是否合法（如分辨率是否为 2 的幂、补丁长度是否 > 0）
    void ComputeWaveVectors();         // 生成 N×N 离散波矢量网格 k = (2πn/L, 2πm/L)
    void ComputeInitialSpectrum();     // 用 Phillips 谱和高斯随机数生成初始复振幅 h0(k) 及其共轭 h0*(-k)
    void ComputeSpectrumAtTime(float timeSeconds); // 根据时间 t 和色散关系 ω(k) 计算演化后的频谱（高度、斜率、位移等）
    void ExecuteInverseFFTs();         // 对所有频谱数据执行逆 FFT，将频域数据变换为空间域数据
    void AssembleSpatialFrame();       // 将 IFFT 结果组装成 CPUWaterSurfaceFrame（包含高度、位移、法线等信息）

    float PhillipsSpectrum(glm::vec2 waveVector) const; // 根据波矢量计算 Phillips 频谱值，包含方向性、风速影响、短波阻尼等

    size_t Index(uint32_t x, uint32_t z) const;        // 将二维频域索引 (x, z) 映射为一维数组索引

    static bool IsPowerOfTwo(uint32_t value);          // 检查值是否为 2 的幂（FFT 要求分辨率满足此项）
    static int SignedFrequencyIndex(uint32_t i, uint32_t n); // 将无符号频域索引转换为有符号索引，用于频谱对称性处理

private:
    TessendorfSpectrumParams m_Params; // 频谱配置参数（分辨率、风力、重力等）
    uint32_t m_N = 0;                  // 网格分辨率（= params.resolution），N×N 频域/空间域网格

    std::vector<glm::vec2> m_WaveVectors;    // 波矢量数组 k = (kx, kz)，大小为 N×N
    std::vector<float> m_WaveNumbers;        // 波数数组 k = |k|，用于色散和频谱计算
    std::vector<float> m_Dispersion;         // 角频率数组 ω(k) = √(gk)，控制各频率波的相位旋转速度
    std::vector<float> m_PhillipsValues;     // Phillips 频谱值数组 Ph(k)，存储每个波矢量的能量分布

    std::vector<std::complex<float>> m_H0;            // 初始频域振幅 h0(k)，复数形式，包含随机相位
    std::vector<std::complex<float>> m_H0MinusConjugate;   // h0*(-k)，即初始振幅的共轭翻转项，保证实数高度场

    // 以下为时间演化后的频域数据，均在 ComputeSpectrumAtTime 中计算
    // Spectrum（频域）：存储的是每个波矢量 对应的复数振幅等物理量 
    std::vector<std::complex<float>> m_HeightSpectrum;        // 高度频谱 ̃h(k,t)，IFFT 后得到空间域高度
    std::vector<std::complex<float>> m_SlopeXSpectrum;        // X 方向斜率频谱，IFFT 后得到 ∂h/∂x
    std::vector<std::complex<float>> m_SlopeZSpectrum;        // Z 方向斜率频谱，IFFT 后得到 ∂h/∂z
    std::vector<std::complex<float>> m_DisplacementXSpectrum; // X 方向水平位移频谱，IFFT 后得到水平偏移 Δx
    std::vector<std::complex<float>> m_DisplacementZSpectrum; // Z 方向水平位移频谱，IFFT 后得到水平偏移 Δz
    std::vector<std::complex<float>> m_DDxdxSpectrum;         // 位移 Jacobian 项 ∂(Δx)/∂x 的频谱
    std::vector<std::complex<float>> m_DDzdzSpectrum;         // 位移 Jacobian 项 ∂(Δz)/∂z 的频谱
    std::vector<std::complex<float>> m_DDxdzSpectrum;         // 位移 Jacobian 项 ∂(Δx)/∂z 的频谱

    // Spatial（空间域）：存储的是每个格点 高度、位移、斜率 等物理量
    std::vector<std::complex<float>> m_HeightSpatial;         // IFFT 后的高度场，大小为 N×N
    std::vector<std::complex<float>> m_SlopeXSpatial;         // IFFT 后的 X 方向斜率场
    std::vector<std::complex<float>> m_SlopeZSpatial;         // IFFT 后的 Z 方向斜率场
    std::vector<std::complex<float>> m_DisplacementXSpatial;  // IFFT 后的 X 方向水平位移场
    std::vector<std::complex<float>> m_DisplacementZSpatial;  // IFFT 后的 Z 方向水平位移场
    std::vector<std::complex<float>> m_DDxdxSpatial;          // IFFT 后的位移 Jacobian ∂(Δx)/∂x
    std::vector<std::complex<float>> m_DDzdzSpatial;          // IFFT 后的位移 Jacobian ∂(Δz)/∂z
    std::vector<std::complex<float>> m_DDxdzSpatial;          // IFFT 后的位移 Jacobian ∂(Δx)/∂z
    
    CPUWaterSurfaceFrame m_Frame; // 当前帧的空间域水面数据：高度场、位移场、法线场、Jacobian 等
    float m_Time = 0.0f;          // 当前模拟累积时间（秒）
    float m_LastHermitianMaxError = 0.0f;       // 最后一次逆 FFT 后的最大 Hermitian 误差
    float m_LastMaxImaginaryResidual = 0.0f;    // 最后一次逆 FFT 后的最大虚部残差
};

}