#include "scene/water/gpu/GPUFFT2D.h"

#include <glm/geometric.hpp>

#include <stdexcept>

namespace water
{
GPUFFT2D::GPUFFT2D(
    VkPhysicalDevice physicalDevice,
    VkDevice device,
    vkp::CommandPool& commandPool,
    VkQueue queue,
    uint32_t resolution,
    uint32_t frameCount,
    const WSTessendorfCPU& cpuSource
)
    : m_PhysicalDevice(physicalDevice),
      m_Device(device),
      m_CommandPool(&commandPool),
      m_Queue(queue),
      m_Resolution(resolution)
{
    const VkDeviceSize elementCount =
        static_cast<VkDeviceSize>(m_Resolution) *
        static_cast<VkDeviceSize>(m_Resolution);

    m_ComplexFieldSize =
        elementCount *
        sizeof(Complex);

    m_PackedFieldSize =
        elementCount *
        sizeof(glm::vec4);

    CreateStaticResources(cpuSource);
    CreateFrameResources(frameCount);
}

uint32_t GPUFFT2D::GetResolution() const
{
    return m_Resolution;
}

VkDeviceSize GPUFFT2D::GetComplexFieldSize() const
{
    return m_ComplexFieldSize;
}

VkDeviceSize GPUFFT2D::GetPackedFieldSize() const
{
    return m_PackedFieldSize;
}

const GPUFFTStaticResources& GPUFFT2D::GetStaticResources() const
{
    return m_StaticResources;
}

const GPUFFTFrameResources& GPUFFT2D::GetFrameResources(uint32_t frameIndex) const
{
    return m_FrameResources.at(frameIndex);
}

GPUFFTFrameResources& GPUFFT2D::GetFrameResources(uint32_t frameIndex)
{
    return m_FrameResources.at(frameIndex);
}

std::unique_ptr<vkp::Buffer> GPUFFT2D::CreateDeviceLocalBufferFromData(
    const void* data,
    VkDeviceSize size,
    VkBufferUsageFlags usage
)
{   // 通过 staging buffer 将打包好的数据上传到 DEVICE_LOCAL 缓冲区
    vkp::Buffer stagingBuffer(
        m_PhysicalDevice,
        m_Device,
        size,
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
    );

    stagingBuffer.Map();
    stagingBuffer.CopyToMapped(data, size);
    stagingBuffer.Unmap();

    auto deviceBuffer = std::make_unique<vkp::Buffer>(
        m_PhysicalDevice,
        m_Device,
        size,
        usage | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT
    );

    m_CommandPool->CopyBuffer(
        m_Device,
        m_Queue,
        stagingBuffer,
        *deviceBuffer,
        size
    );

    return deviceBuffer;
}

std::vector<Complex> GPUFFT2D::ConvertComplexField(
    const std::vector<std::complex<float>>& source
) const
{   // 将 std::complex<float> 转换为 GPU 友好的 Complex 结构体
    std::vector<Complex> result(source.size());

    for(size_t i = 0; i < source.size(); i++){
        result[i].real = source[i].real();
        result[i].imag = source[i].imag();
    }

    return result;
}

std::vector<GPUWaveData> GPUFFT2D::BuildWaveData(
    const WSTessendorfCPU& cpuSource
) const
{   // wave data 构造
    const std::vector<glm::vec2>& waveVectors =
        cpuSource.GetWaveVectors();

    const std::vector<float>& dispersion =
        cpuSource.GetDispersionField();

    if(waveVectors.size() != dispersion.size()){
        throw std::runtime_error("GPUFFT2D wave data size mismatch");
    }

    std::vector<GPUWaveData> result(waveVectors.size());

    for(size_t i = 0; i < waveVectors.size(); i++){
        result[i].waveVector = waveVectors[i];
        result[i].waveNumber = glm::length(waveVectors[i]);
        result[i].dispersion = dispersion[i];
    }

    return result;
}

void GPUFFT2D::CreateStaticResources(const WSTessendorfCPU& cpuSource)
{   // 静态资源创建
    std::vector<Complex> h0 =
        ConvertComplexField(cpuSource.GetH0Field());

    std::vector<Complex> h0MinusConjugate =
        ConvertComplexField(cpuSource.GetH0MinusConjugateField());

    std::vector<GPUWaveData> waveData =
        BuildWaveData(cpuSource);

    if(h0.size() != h0MinusConjugate.size() ||
        h0.size() != waveData.size()){
        throw std::runtime_error("GPUFFT2D static resource size mismatch");
    }

    const VkDeviceSize complexBufferSize =
        static_cast<VkDeviceSize>(h0.size()) *
        sizeof(Complex);

    const VkDeviceSize waveDataBufferSize =
        static_cast<VkDeviceSize>(waveData.size()) *
        sizeof(GPUWaveData);

    m_StaticResources.h0Buffer =
        CreateDeviceLocalBufferFromData(
            h0.data(),
            complexBufferSize,
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT
        );

    m_StaticResources.h0MinusConjugateBuffer =
        CreateDeviceLocalBufferFromData(
            h0MinusConjugate.data(),
            complexBufferSize,
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT
        );

    m_StaticResources.waveDataBuffer =
        CreateDeviceLocalBufferFromData(
            waveData.data(),
            waveDataBufferSize,
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT
        );
}

void GPUFFT2D::CreateFrameResources(uint32_t frameCount)
{   // per-frame 资源创建
    m_FrameResources.clear();
    m_FrameResources.resize(frameCount);

    VkImageUsageFlags imageUsage =
        VK_IMAGE_USAGE_STORAGE_BIT |
        VK_IMAGE_USAGE_SAMPLED_BIT |
        VK_IMAGE_USAGE_TRANSFER_SRC_BIT;

    for(GPUFFTFrameResources& resources : m_FrameResources){
        for(uint32_t i = 0; i < 4; i++){
            resources.spectrumPing[i] = std::make_unique<vkp::Buffer>(
                m_PhysicalDevice,
                m_Device,
                m_PackedFieldSize,
                VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
                    VK_BUFFER_USAGE_TRANSFER_SRC_BIT |
                    VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT
            );

            resources.spectrumPong[i] = std::make_unique<vkp::Buffer>(
                m_PhysicalDevice,
                m_Device,
                m_PackedFieldSize,
                VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
                    VK_BUFFER_USAGE_TRANSFER_SRC_BIT |
                    VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT
            );
        }

        resources.displacementImage = std::make_unique<DynamicImage2D>(
            m_PhysicalDevice,
            m_Device,
            m_Resolution,
            m_Resolution,
            VK_FORMAT_R32G32B32A32_SFLOAT,
            imageUsage
        );

        resources.normalAuxImage = std::make_unique<DynamicImage2D>(
            m_PhysicalDevice,
            m_Device,
            m_Resolution,
            m_Resolution,
            VK_FORMAT_R32G32B32A32_SFLOAT,
            imageUsage
        );
    }
}
}