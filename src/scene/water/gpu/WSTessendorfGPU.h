#pragma once

#include "scene/water/gpu/ComputePipeline.h"
#include "scene/water/gpu/GPUFFT2D.h"
#include "scene/water/sources/IGPUWaterSurfaceSource.h"
#include "scene/water/sources/WSTessendorfCPU.h"
#include "scene/water/sources/WSTessendorfCascadesCPU.h"

#include "vulkan/Descriptors.h"

#include <array>
#include <cstdint>
#include <memory>
#include <vector>
#include <vulkan/vulkan.h>

namespace water
{
struct SpectrumPushConstants
{
    uint32_t resolution = 0;
    float time = 0.0f;
};

struct FFTPushConstants
{
    uint32_t resolution = 0;
    uint32_t stage = 0;
    uint32_t direction = 0;
    uint32_t inverse = 1;
};

struct OutputPushConstants
{
    uint32_t resolution = 0;
    float choppyLambda = 1.0f;
};

class WSTessendorfGPU final : public IGPUWaterSurfaceSource
{
public:
    WSTessendorfGPU(
        VkPhysicalDevice physicalDevice,
        VkDevice device,
        vkp::CommandPool& commandPool,
        VkQueue queue,
        uint32_t frameCount,
        VkSampler sampler,
        const MultiCascadeParams& params,
        const std::array<float, kMaxFFTCascades>& amplitudeScales,
        SpectrumInitializationMode initializationMode = SpectrumInitializationMode::CPUReferenceUpload
    );


    void UpdateGPU(
        VkCommandBuffer commandBuffer,
        uint32_t frameIndex,
        float deltaTime
    ) override;

    const WaterSurfaceGPUResources& GetGPUResources(uint32_t frameIndex) const override;

    uint32_t GetResolution() const;
    float GetCascadePatchLength(uint32_t cascadeIndex) const;

    VkDescriptorImageInfo GetFrameDisplacementInfo(uint32_t frameIndex) const;
    VkDescriptorImageInfo GetFrameNormalAuxInfo(uint32_t frameIndex) const;
    VkDescriptorImageInfo GetFrameDisplacementInfo(uint32_t frameIndex, uint32_t cascadeIndex) const;
    VkDescriptorImageInfo GetFrameNormalAuxInfo(uint32_t frameIndex, uint32_t cascadeIndex) const;

private:
    void InitializeImagesToGeneral();

    void CreateDescriptorSetLayouts();
    void CreatePipelines();
    void CreateDescriptorPool();
    void CreateDescriptorSets();

    void RecordSpectrumUpdate(VkCommandBuffer commandBuffer, uint32_t cascadeIndex);
    bool RecordRowFFT(VkCommandBuffer commandBuffer, uint32_t cascadeIndex, bool inputIsPing);
    bool RecordColumnFFT(VkCommandBuffer commandBuffer, uint32_t cascadeIndex, bool inputIsPing);
    void RecordOutput(VkCommandBuffer commandBuffer, uint32_t cascadeIndex, bool inputIsPing);

    void RecordBufferBarrier(
        VkCommandBuffer commandBuffer,
        VkBuffer buffer,
        VkDeviceSize size,
        VkAccessFlags srcAccess,
        VkAccessFlags dstAccess,
        VkPipelineStageFlags srcStage,
        VkPipelineStageFlags dstStage
    );

    void RecordPackedBarrier(
        VkCommandBuffer commandBuffer,
        uint32_t cascadeIndex,
        GPUFFTFrameResources& resources,
        bool ping,
        VkAccessFlags srcAccess,
        VkAccessFlags dstAccess,
        VkPipelineStageFlags srcStage,
        VkPipelineStageFlags dstStage
    );

    uint32_t Log2(uint32_t value) const;

private:
    VkPhysicalDevice m_PhysicalDevice = VK_NULL_HANDLE;
    VkDevice m_Device = VK_NULL_HANDLE;
    vkp::CommandPool* m_CommandPool = nullptr;
    VkQueue m_Queue = VK_NULL_HANDLE;
    VkSampler m_Sampler = VK_NULL_HANDLE;

    uint32_t m_FrameCount = 0;
    uint32_t m_FrameIndex = 0;
    uint32_t m_Resolution = 0;

    float m_Time = 0.0f;
    std::array<float, kMaxFFTCascades> m_PatchLengths{};
    std::array<float, kMaxFFTCascades> m_AmplitudeScales{1.0f, 1.0f, 1.0f};
    float m_ChoppyLambda = 1.0f;

    std::array<std::unique_ptr<WSTessendorfCPU>, kMaxFFTCascades> m_CPUInitialSources;
    std::array<std::unique_ptr<GPUFFT2D>, kMaxFFTCascades> m_GPUFFTs;

    std::unique_ptr<vkp::DescriptorSetLayout> m_SpectrumSetLayout;
    std::unique_ptr<vkp::DescriptorSetLayout> m_StockhamSetLayout;
    std::unique_ptr<vkp::DescriptorSetLayout> m_OutputSetLayout;
    std::unique_ptr<vkp::DescriptorPool> m_DescriptorPool;

    std::unique_ptr<ComputePipeline> m_SpectrumPipeline;
    std::unique_ptr<ComputePipeline> m_StockhamPipeline;
    std::unique_ptr<ComputePipeline> m_OutputPipeline;

    std::array<std::vector<VkDescriptorSet>, kMaxFFTCascades> m_SpectrumSets;
    std::array<std::vector<VkDescriptorSet>, kMaxFFTCascades> m_PingToPongSets;
    std::array<std::vector<VkDescriptorSet>, kMaxFFTCascades> m_PongToPingSets;
    std::array<std::vector<VkDescriptorSet>, kMaxFFTCascades> m_OutputPingSets;
    std::array<std::vector<VkDescriptorSet>, kMaxFFTCascades> m_OutputPongSets;

    std::vector<WaterSurfaceGPUResources> m_GPUResourcesPerFrame;

    SpectrumInitializationMode m_InitializationMode =
        SpectrumInitializationMode::CPUReferenceUpload;
};
}
