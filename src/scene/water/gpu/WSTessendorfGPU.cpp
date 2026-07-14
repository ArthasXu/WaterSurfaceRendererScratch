#include "scene/water/gpu/WSTessendorfGPU.h"

#include <glm/geometric.hpp>

#include <array>
#include <stdexcept>

namespace water
{
WSTessendorfGPU::WSTessendorfGPU(
    VkPhysicalDevice physicalDevice,
    VkDevice device,
    vkp::CommandPool& commandPool,
    VkQueue queue,
    uint32_t frameCount,
    VkSampler sampler,
    const TessendorfSpectrumParams& params
)
    : m_PhysicalDevice(physicalDevice),
      m_Device(device),
      m_CommandPool(&commandPool),
      m_Queue(queue),
      m_Sampler(sampler),
      m_FrameCount(frameCount),
      m_Resolution(params.resolution),
      m_PatchLength(params.patchLength),
      m_ChoppyLambda(params.choppyLambda)
{
    m_CPUInitialSource = std::make_unique<WSTessendorfCPU>(params);

    m_GPUFFT = std::make_unique<GPUFFT2D>(
        m_PhysicalDevice,
        m_Device,
        *m_CommandPool,
        m_Queue,
        m_Resolution,
        m_FrameCount,
        *m_CPUInitialSource
    );

    InitializeImagesToGeneral();

    CreateDescriptorSetLayouts();
    CreatePipelines();
    CreateDescriptorPool();
    CreateDescriptorSets();
}

void WSTessendorfGPU::SetFrameIndex(uint32_t frameIndex)
{
    m_FrameIndex = frameIndex;
}

const WaterSurfaceGPUResources& WSTessendorfGPU::GetGPUResources() const
{
    return m_GPUResources;
}

uint32_t WSTessendorfGPU::GetResolution() const
{
    return m_Resolution;
}

float WSTessendorfGPU::GetPatchLength() const
{
    return m_PatchLength;
}

VkDescriptorImageInfo WSTessendorfGPU::GetFrameDisplacementInfo(uint32_t frameIndex) const
{
    const GPUFFTFrameResources& frame =
        m_GPUFFT->GetFrameResources(frameIndex);

    return frame.displacementImage->GetGeneralSampledDescriptorInfo(m_Sampler);
}

VkDescriptorImageInfo WSTessendorfGPU::GetFrameNormalAuxInfo(uint32_t frameIndex) const
{
    const GPUFFTFrameResources& frame =
        m_GPUFFT->GetFrameResources(frameIndex);

    return frame.normalAuxImage->GetGeneralSampledDescriptorInfo(m_Sampler);
}

// 初始化 image layout 到 GENERAL
void WSTessendorfGPU::InitializeImagesToGeneral()
{
    VkCommandBuffer commandBuffer =
        m_CommandPool->BeginOneTimeCommands(m_Device);

    for(uint32_t frameIndex = 0; frameIndex < m_FrameCount; frameIndex++){
        GPUFFTFrameResources& frame =
            m_GPUFFT->GetFrameResources(frameIndex);

        frame.displacementImage->RecordTransitionToGeneral(commandBuffer);
        frame.normalAuxImage->RecordTransitionToGeneral(commandBuffer);
    }

    m_CommandPool->EndOneTimeCommands(
        m_Device,
        m_Queue,
        commandBuffer
    );
}

void WSTessendorfGPU::CreateDescriptorSetLayouts()
{
    m_SpectrumSetLayout = DescriptorSetLayout::Builder(m_Device)
        .AddBinding(0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT)
        .AddBinding(1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT)
        .AddBinding(2, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT)
        .AddBinding(3, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT)
        .AddBinding(4, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT)
        .AddBinding(5, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT)
        .AddBinding(6, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT)
        .Build();

    m_StockhamSetLayout = DescriptorSetLayout::Builder(m_Device)
        .AddBinding(0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT)
        .AddBinding(1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT)
        .AddBinding(2, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT)
        .AddBinding(3, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT)
        .AddBinding(4, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT)
        .AddBinding(5, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT)
        .AddBinding(6, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT)
        .AddBinding(7, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT)
        .Build();

    m_OutputSetLayout = DescriptorSetLayout::Builder(m_Device)
        .AddBinding(0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT)
        .AddBinding(1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT)
        .AddBinding(2, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT)
        .AddBinding(3, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT)
        .AddBinding(4, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_SHADER_STAGE_COMPUTE_BIT)
        .AddBinding(5, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_SHADER_STAGE_COMPUTE_BIT)
        .Build();
}

void WSTessendorfGPU::CreatePipelines()
{
    ComputePipelineConfig spectrumConfig{};
    spectrumConfig.descriptorSetLayouts = {*m_SpectrumSetLayout};
    spectrumConfig.enablePushConstants = true;
    spectrumConfig.pushConstantSize = sizeof(SpectrumPushConstants);

    m_SpectrumPipeline = std::make_unique<ComputePipeline>(
        m_Device,
        "shaders/water/fft/spectrum_update.comp.spv",
        spectrumConfig
    );

    ComputePipelineConfig stockhamConfig{};
    stockhamConfig.descriptorSetLayouts = {*m_StockhamSetLayout};
    stockhamConfig.enablePushConstants = true;
    stockhamConfig.pushConstantSize = sizeof(FFTPushConstants);

    m_StockhamPipeline = std::make_unique<ComputePipeline>(
        m_Device,
        "shaders/water/fft/fft_stockham.comp.spv",
        stockhamConfig
    );

    ComputePipelineConfig outputConfig{};
    outputConfig.descriptorSetLayouts = {*m_OutputSetLayout};
    outputConfig.enablePushConstants = true;
    outputConfig.pushConstantSize = sizeof(OutputPushConstants);

    m_OutputPipeline = std::make_unique<ComputePipeline>(
        m_Device,
        "shaders/water/fft/fft_output.comp.spv",
        outputConfig
    );
}

// 每 frame:
// spectrum = 7 storage buffers
// ping->pong = 8
// pong->ping = 8
// output = 4 storage buffers + 2 storage images
// 总 storage buffer = 27，留 29
void WSTessendorfGPU::CreateDescriptorPool()
{
    m_DescriptorPool = DescriptorPool::Builder(m_Device)
        .SetMaxSets(m_FrameCount * 4)
        .AddPoolSize(
            VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            m_FrameCount * 29
        )
        .AddPoolSize(
            VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
            m_FrameCount * 2
        )
        .Build();
}

void WSTessendorfGPU::CreateDescriptorSets()
{
    m_SpectrumSets.resize(m_FrameCount);
    m_PingToPongSets.resize(m_FrameCount);
    m_PongToPingSets.resize(m_FrameCount);
    m_OutputSets.resize(m_FrameCount);

    const GPUFFTStaticResources& staticResources =
        m_GPUFFT->GetStaticResources();

    VkDescriptorBufferInfo h0Info{};
    h0Info.buffer = *staticResources.h0Buffer;
    h0Info.offset = 0;
    h0Info.range = m_GPUFFT->GetComplexFieldSize();

    VkDescriptorBufferInfo h0MinusInfo{};
    h0MinusInfo.buffer = *staticResources.h0MinusConjugateBuffer;
    h0MinusInfo.offset = 0;
    h0MinusInfo.range = m_GPUFFT->GetComplexFieldSize();

    VkDescriptorBufferInfo waveDataInfo{};
    waveDataInfo.buffer = *staticResources.waveDataBuffer;
    waveDataInfo.offset = 0;
    waveDataInfo.range =
        static_cast<VkDeviceSize>(m_Resolution) *
        static_cast<VkDeviceSize>(m_Resolution) *
        sizeof(GPUWaveData);

    for(uint32_t frameIndex = 0; frameIndex < m_FrameCount; frameIndex++){
    GPUFFTFrameResources& frame =
        m_GPUFFT->GetFrameResources(frameIndex);

    std::array<VkDescriptorBufferInfo, 4> pingInfos{};
    std::array<VkDescriptorBufferInfo, 4> pongInfos{};

    for(uint32_t i = 0; i < 4; i++){
        pingInfos[i].buffer = *frame.spectrumPing[i];
        pingInfos[i].offset = 0;
        pingInfos[i].range = m_GPUFFT->GetPackedFieldSize();

        pongInfos[i].buffer = *frame.spectrumPong[i];
        pongInfos[i].offset = 0;
        pongInfos[i].range = m_GPUFFT->GetPackedFieldSize();
    }

    bool spectrumSuccess =
        DescriptorWriter(*m_SpectrumSetLayout, *m_DescriptorPool)
            .WriteBuffer(0, &h0Info)
            .WriteBuffer(1, &h0MinusInfo)
            .WriteBuffer(2, &waveDataInfo)
            .WriteBuffer(3, &pingInfos[0])
            .WriteBuffer(4, &pingInfos[1])
            .WriteBuffer(5, &pingInfos[2])
            .WriteBuffer(6, &pingInfos[3])
            .Build(m_SpectrumSets[frameIndex]);

    if(!spectrumSuccess){
        throw std::runtime_error("Failed to build GPU spectrum descriptor set");
    }

    bool pingToPongSuccess =
        DescriptorWriter(*m_StockhamSetLayout, *m_DescriptorPool)
            .WriteBuffer(0, &pingInfos[0])
            .WriteBuffer(1, &pingInfos[1])
            .WriteBuffer(2, &pingInfos[2])
            .WriteBuffer(3, &pingInfos[3])
            .WriteBuffer(4, &pongInfos[0])
            .WriteBuffer(5, &pongInfos[1])
            .WriteBuffer(6, &pongInfos[2])
            .WriteBuffer(7, &pongInfos[3])
            .Build(m_PingToPongSets[frameIndex]);

    if(!pingToPongSuccess){
        throw std::runtime_error("Failed to build GPU ping to pong descriptor set");
    }

    bool pongToPingSuccess =
        DescriptorWriter(*m_StockhamSetLayout, *m_DescriptorPool)
            .WriteBuffer(0, &pongInfos[0])
            .WriteBuffer(1, &pongInfos[1])
            .WriteBuffer(2, &pongInfos[2])
            .WriteBuffer(3, &pongInfos[3])
            .WriteBuffer(4, &pingInfos[0])
            .WriteBuffer(5, &pingInfos[1])
            .WriteBuffer(6, &pingInfos[2])
            .WriteBuffer(7, &pingInfos[3])
            .Build(m_PongToPingSets[frameIndex]);

    if(!pongToPingSuccess){
        throw std::runtime_error("Failed to build GPU pong to ping descriptor set");
    }

    VkDescriptorImageInfo displacementStorageInfo =
            frame.displacementImage->GetStorageDescriptorInfo();

        VkDescriptorImageInfo normalAuxStorageInfo =
            frame.normalAuxImage->GetStorageDescriptorInfo();

        bool outputSuccess =
            DescriptorWriter(*m_OutputSetLayout, *m_DescriptorPool)
                .WriteBuffer(0, &pingInfos[0])
                .WriteBuffer(1, &pingInfos[1])
                .WriteBuffer(2, &pingInfos[2])
                .WriteBuffer(3, &pingInfos[3])
                .WriteImage(4, &displacementStorageInfo)
                .WriteImage(5, &normalAuxStorageInfo)
                .Build(m_OutputSets[frameIndex]);

        if(!outputSuccess){
            throw std::runtime_error("Failed to build GPU output descriptor set");
        }
    }

    GPUFFTFrameResources& frame0 =
        m_GPUFFT->GetFrameResources(0);

    m_GPUResources.cascadeCount = 1;
    m_GPUResources.cascades[0].displacement =
        frame0.displacementImage->GetGeneralSampledDescriptorInfo(m_Sampler);
    m_GPUResources.cascades[0].normalAux =
        frame0.normalAuxImage->GetGeneralSampledDescriptorInfo(m_Sampler);
    m_GPUResources.cascades[0].patchLength = m_PatchLength;
    m_GPUResources.cascades[0].resolution = m_Resolution;
    m_GPUResources.cascades[0].amplitudeScale = 1.0f;
}

void WSTessendorfGPU::RecordBufferBarrier(
    VkCommandBuffer commandBuffer,
    VkBuffer buffer,
    VkDeviceSize size,
    VkAccessFlags srcAccess,
    VkAccessFlags dstAccess,
    VkPipelineStageFlags srcStage,
    VkPipelineStageFlags dstStage
)
{
    VkBufferMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
    barrier.srcAccessMask = srcAccess;
    barrier.dstAccessMask = dstAccess;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.buffer = buffer;
    barrier.offset = 0;
    barrier.size = size;

    vkCmdPipelineBarrier(
        commandBuffer,
        srcStage,
        dstStage,
        0,
        0,
        nullptr,
        1,
        &barrier,
        0,
        nullptr
    );
}

void WSTessendorfGPU::RecordPackedBarrier(
    VkCommandBuffer commandBuffer,
    GPUFFTFrameResources& resources,
    bool ping,
    VkAccessFlags srcAccess,
    VkAccessFlags dstAccess,
    VkPipelineStageFlags srcStage,
    VkPipelineStageFlags dstStage
)
{
    for(uint32_t i = 0; i < 4; i++){
        VkBuffer buffer = ping
            ? *resources.spectrumPing[i]
            : *resources.spectrumPong[i];

        RecordBufferBarrier(
            commandBuffer,
            buffer,
            m_GPUFFT->GetPackedFieldSize(),
            srcAccess,
            dstAccess,
            srcStage,
            dstStage
        );
    }
}

uint32_t WSTessendorfGPU::Log2(uint32_t value) const
{
    uint32_t result = 0;

    while(value > 1){
        value >>= 1;
        result++;
    }

    return result;
}

void WSTessendorfGPU::UpdateGPU(
    VkCommandBuffer commandBuffer,
    float deltaTime
)
{
    m_Time += deltaTime;

    GPUFFTFrameResources& frame =
        m_GPUFFT->GetFrameResources(m_FrameIndex);

    frame.displacementImage->RecordGraphicsReadToComputeWriteBarrier(commandBuffer);
    frame.normalAuxImage->RecordGraphicsReadToComputeWriteBarrier(commandBuffer);

    RecordSpectrumUpdate(commandBuffer);

    RecordPackedBarrier(
        commandBuffer,
        frame,
        true,
        VK_ACCESS_SHADER_WRITE_BIT,
        VK_ACCESS_SHADER_READ_BIT,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT
    );

    RecordRowFFT(commandBuffer);
    RecordColumnFFT(commandBuffer);
    RecordOutput(commandBuffer);

    frame.displacementImage->RecordComputeWriteToGraphicsReadBarrier(commandBuffer);
    frame.normalAuxImage->RecordComputeWriteToGraphicsReadBarrier(commandBuffer);
}

void WSTessendorfGPU::RecordSpectrumUpdate(VkCommandBuffer commandBuffer)
{
    vkCmdBindPipeline(
        commandBuffer,
        VK_PIPELINE_BIND_POINT_COMPUTE,
        *m_SpectrumPipeline
    );

    VkDescriptorSet descriptorSet =
        m_SpectrumSets[m_FrameIndex];

    vkCmdBindDescriptorSets(
        commandBuffer,
        VK_PIPELINE_BIND_POINT_COMPUTE,
        m_SpectrumPipeline->GetLayout(),
        0,
        1,
        &descriptorSet,
        0,
        nullptr
    );

    SpectrumPushConstants push{};
    push.resolution = m_Resolution;
    push.time = m_Time;

    vkCmdPushConstants(
        commandBuffer,
        m_SpectrumPipeline->GetLayout(),
        VK_SHADER_STAGE_COMPUTE_BIT,
        0,
        sizeof(SpectrumPushConstants),
        &push
    );

    uint32_t total =
        m_Resolution *
        m_Resolution;

    uint32_t groupCount =
        (total + 63) / 64;

    vkCmdDispatch(commandBuffer, groupCount, 1, 1);
}

void WSTessendorfGPU::RecordRowFFT(VkCommandBuffer commandBuffer)
{
    GPUFFTFrameResources& frame =
        m_GPUFFT->GetFrameResources(m_FrameIndex);

    vkCmdBindPipeline(
        commandBuffer,
        VK_PIPELINE_BIND_POINT_COMPUTE,
        *m_StockhamPipeline
    );

    uint32_t logN = Log2(m_Resolution);
    uint32_t butterflyCount =
        (m_Resolution * m_Resolution) / 2;

    uint32_t groupCount =
        (butterflyCount + 63) / 64;

    bool currentPing = true;

    for(uint32_t stage = 0; stage < logN; stage++){
        VkDescriptorSet descriptorSet =
            currentPing
                ? m_PingToPongSets[m_FrameIndex]
                : m_PongToPingSets[m_FrameIndex];

        vkCmdBindDescriptorSets(
            commandBuffer,
            VK_PIPELINE_BIND_POINT_COMPUTE,
            m_StockhamPipeline->GetLayout(),
            0,
            1,
            &descriptorSet,
            0,
            nullptr
        );

        FFTPushConstants push{};
        push.resolution = m_Resolution;
        push.stage = stage;
        push.direction = 0;
        push.inverse = 1;

        vkCmdPushConstants(
            commandBuffer,
            m_StockhamPipeline->GetLayout(),
            VK_SHADER_STAGE_COMPUTE_BIT,
            0,
            sizeof(FFTPushConstants),
            &push
        );

        vkCmdDispatch(commandBuffer, groupCount, 1, 1);

        bool outputIsPing = !currentPing;

        RecordPackedBarrier(
            commandBuffer,
            frame,
            outputIsPing,
            VK_ACCESS_SHADER_WRITE_BIT,
            VK_ACCESS_SHADER_READ_BIT,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT
        );

        currentPing = !currentPing;
    }
}

void WSTessendorfGPU::RecordColumnFFT(VkCommandBuffer commandBuffer)
{
    GPUFFTFrameResources& frame =
        m_GPUFFT->GetFrameResources(m_FrameIndex);

    vkCmdBindPipeline(
        commandBuffer,
        VK_PIPELINE_BIND_POINT_COMPUTE,
        *m_StockhamPipeline
    );

    uint32_t logN = Log2(m_Resolution);
    uint32_t butterflyCount =
        (m_Resolution * m_Resolution) / 2;

    uint32_t groupCount =
        (butterflyCount + 63) / 64;

    bool currentPing = true;

    for(uint32_t stage = 0; stage < logN; stage++){
        VkDescriptorSet descriptorSet =
            currentPing
                ? m_PingToPongSets[m_FrameIndex]
                : m_PongToPingSets[m_FrameIndex];

        vkCmdBindDescriptorSets(
            commandBuffer,
            VK_PIPELINE_BIND_POINT_COMPUTE,
            m_StockhamPipeline->GetLayout(),
            0,
            1,
            &descriptorSet,
            0,
            nullptr
        );

        FFTPushConstants push{};
        push.resolution = m_Resolution;
        push.stage = stage;
        push.direction = 1;
        push.inverse = 1;

        vkCmdPushConstants(
            commandBuffer,
            m_StockhamPipeline->GetLayout(),
            VK_SHADER_STAGE_COMPUTE_BIT,
            0,
            sizeof(FFTPushConstants),
            &push
        );

        vkCmdDispatch(commandBuffer, groupCount, 1, 1);

        bool outputIsPing = !currentPing;

        RecordPackedBarrier(
            commandBuffer,
            frame,
            outputIsPing,
            VK_ACCESS_SHADER_WRITE_BIT,
            VK_ACCESS_SHADER_READ_BIT,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT
        );

        currentPing = !currentPing;
    }
}

void WSTessendorfGPU::RecordOutput(VkCommandBuffer commandBuffer)
{
    vkCmdBindPipeline(
        commandBuffer,
        VK_PIPELINE_BIND_POINT_COMPUTE,
        *m_OutputPipeline
    );

    VkDescriptorSet descriptorSet =
        m_OutputSets[m_FrameIndex];

    vkCmdBindDescriptorSets(
        commandBuffer,
        VK_PIPELINE_BIND_POINT_COMPUTE,
        m_OutputPipeline->GetLayout(),
        0,
        1,
        &descriptorSet,
        0,
        nullptr
    );

    OutputPushConstants push{};
    push.resolution = m_Resolution;
    push.choppyLambda = m_ChoppyLambda;

    vkCmdPushConstants(
        commandBuffer,
        m_OutputPipeline->GetLayout(),
        VK_SHADER_STAGE_COMPUTE_BIT,
        0,
        sizeof(OutputPushConstants),
        &push
    );

    uint32_t total =
        m_Resolution *
        m_Resolution;

    uint32_t groupCount =
        (total + 63) / 64;

    vkCmdDispatch(commandBuffer, groupCount, 1, 1);
}

}