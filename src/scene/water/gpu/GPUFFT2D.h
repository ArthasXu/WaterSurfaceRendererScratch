#pragma once

#include "scene/water/common/FFTResourceContract.h"
#include "scene/water/render/DynamicImage2D.h"
#include "scene/water/sources/WSTessendorfCPU.h"

#include "vulkan/Buffer.h"
#include "vulkan/CommandPool.h"

#include <glm/glm.hpp>

#include <array>
#include <complex>
#include <cstdint>
#include <memory>
#include <vector>
#include <vulkan/vulkan.h>

namespace water
{
struct Complex
{
    float real;
    float imag;
};

struct alignas(16) GPUWaveData
{
    glm::vec2 waveVector;
    float waveNumber;
    float dispersion;
};

// 帧资源（GPUFFTFrameResources）：每个飞行帧独立拥有一套，避免多帧并行时的数据竞争
struct GPUFFTFrameResources
{
    // ping-pong 缓冲区，用于在 GPU 上执行 Stockham IFFT 的蝶形运算
    std::array<std::unique_ptr<vkp::Buffer>, 4> spectrumPing;
    std::array<std::unique_ptr<vkp::Buffer>, 4> spectrumPong;

    // displacementImage / normalAuxImage：最终输出的位移图和法线辅助图，供顶点着色器采样
    std::unique_ptr<DynamicImage2D> displacementImage;
    std::unique_ptr<DynamicImage2D> normalAuxImage;
};

// 静态资源（GPUFFTStaticResources）：只在初始化时上传一次，之后每帧复用
struct GPUFFTStaticResources
{
    std::unique_ptr<vkp::Buffer> h0Buffer; // 初始复振幅 H0
    std::unique_ptr<vkp::Buffer> h0MinusConjugateBuffer; // 共轭项
    std::unique_ptr<vkp::Buffer> waveDataBuffer; // 每个波矢量的方向、波长、色散频率
};

// CPU端 Tessendorf FFT 迁移到 GPU 计算着色器的核心桥梁类
class GPUFFT2D
{
public:
    GPUFFT2D(
        VkPhysicalDevice physicalDevice,
        VkDevice device,
        vkp::CommandPool& commandPool,
        VkQueue queue,
        uint32_t resolution,
        uint32_t frameCount,
        const WSTessendorfCPU& cpuSource
    );

    GPUFFT2D(const GPUFFT2D&) = delete;
    GPUFFT2D& operator=(const GPUFFT2D&) = delete;

    uint32_t GetResolution() const;
    VkDeviceSize GetComplexFieldSize() const;
    VkDeviceSize GetPackedFieldSize() const;

    const GPUFFTStaticResources& GetStaticResources() const;
    const GPUFFTFrameResources& GetFrameResources(uint32_t frameIndex) const;
    GPUFFTFrameResources& GetFrameResources(uint32_t frameIndex);

private:
    // 通过 staging buffer 将打包好的数据上传到 DEVICE_LOCAL 缓冲区
    std::unique_ptr<vkp::Buffer> CreateDeviceLocalBufferFromData(
        const void* data,
        VkDeviceSize size,
        VkBufferUsageFlags usage
    );

    void CreateStaticResources(const WSTessendorfCPU& cpuSource);
    void CreateFrameResources(uint32_t frameCount);

    // 将 std::complex<float> 转换为 GPU 友好的 Complex 结构体
    std::vector<Complex> ConvertComplexField(
        const std::vector<std::complex<float>>& source
    ) const;

    // 将波矢量、波数、色散打包成 GPUWaveData 结构体
    std::vector<GPUWaveData> BuildWaveData(
        const WSTessendorfCPU& cpuSource
    ) const;

private:
    VkPhysicalDevice m_PhysicalDevice = VK_NULL_HANDLE;
    VkDevice m_Device = VK_NULL_HANDLE;
    vkp::CommandPool* m_CommandPool = nullptr;
    VkQueue m_Queue = VK_NULL_HANDLE;

    uint32_t m_Resolution = 0;
    VkDeviceSize m_ComplexFieldSize = 0;
    VkDeviceSize m_PackedFieldSize = 0;

    GPUFFTStaticResources m_StaticResources;
    std::vector<GPUFFTFrameResources> m_FrameResources;
};


}

