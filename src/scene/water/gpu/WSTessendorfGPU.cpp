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
    const MultiCascadeParams& params
)
    : m_PhysicalDevice(physicalDevice),
      m_Device(device),
      m_CommandPool(&commandPool),
      m_Queue(queue),
      m_Sampler(sampler),
      m_FrameCount(frameCount),
      m_Resolution(params.resolution),
      m_ChoppyLambda(params.baseSpectrum.choppyLambda)
{
    m_PatchLengths = {
        params.shortPatchLength,
        params.midPatchLength,
        params.longPatchLength
    };

    std::array<TessendorfSpectrumParams, kMaxFFTCascades> cascadeParams{};

    cascadeParams[0] = params.baseSpectrum;
    cascadeParams[0].resolution = params.resolution;
    cascadeParams[0].patchLength = params.shortPatchLength;
    cascadeParams[0].randomSeed = params.baseSeed + 0;
    cascadeParams[0].spectrumBand = WSTessendorfCascadesCPU::MakeShortBand();

    cascadeParams[1] = params.baseSpectrum;
    cascadeParams[1].resolution = params.resolution;
    cascadeParams[1].patchLength = params.midPatchLength;
    cascadeParams[1].randomSeed = params.baseSeed + 101;
    cascadeParams[1].spectrumBand = WSTessendorfCascadesCPU::MakeMidBand();

    cascadeParams[2] = params.baseSpectrum;
    cascadeParams[2].resolution = params.resolution;
    cascadeParams[2].patchLength = params.longPatchLength;
    cascadeParams[2].randomSeed = params.baseSeed + 211;
    cascadeParams[2].spectrumBand = WSTessendorfCascadesCPU::MakeLongBand();

    for(uint32_t cascadeIndex = 0; cascadeIndex < kMaxFFTCascades; cascadeIndex++){
        m_CPUInitialSources[cascadeIndex] =
            std::make_unique<WSTessendorfCPU>(cascadeParams[cascadeIndex]);

        m_GPUFFTs[cascadeIndex] =
            std::make_unique<GPUFFT2D>(
                m_PhysicalDevice,
                m_Device,
                *m_CommandPool,
                m_Queue,
                m_Resolution,
                m_FrameCount,
                *m_CPUInitialSources[cascadeIndex]
            );
    }

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
    return m_PatchLengths[1];
}

VkDescriptorImageInfo WSTessendorfGPU::GetFrameDisplacementInfo(uint32_t frameIndex) const
{
    const GPUFFTFrameResources& frame =
        m_GPUFFTs[1]->GetFrameResources(frameIndex);

    return frame.displacementImage->GetGeneralSampledDescriptorInfo(m_Sampler);
}

VkDescriptorImageInfo WSTessendorfGPU::GetFrameNormalAuxInfo(uint32_t frameIndex) const
{
    const GPUFFTFrameResources& frame =
        m_GPUFFTs[1]->GetFrameResources(frameIndex);

    return frame.normalAuxImage->GetGeneralSampledDescriptorInfo(m_Sampler);
}

VkDescriptorImageInfo WSTessendorfGPU::GetFrameDisplacementInfo(
    uint32_t frameIndex,
    uint32_t cascadeIndex
) const
{
    const GPUFFTFrameResources& frame =
        m_GPUFFTs[cascadeIndex]->GetFrameResources(frameIndex);

    return frame.displacementImage->GetGeneralSampledDescriptorInfo(m_Sampler);
}

VkDescriptorImageInfo WSTessendorfGPU::GetFrameNormalAuxInfo(
    uint32_t frameIndex,
    uint32_t cascadeIndex
) const
{
    const GPUFFTFrameResources& frame =
        m_GPUFFTs[cascadeIndex]->GetFrameResources(frameIndex);

    return frame.normalAuxImage->GetGeneralSampledDescriptorInfo(m_Sampler);
}

// 初始化 image layout 到 GENERAL
void WSTessendorfGPU::InitializeImagesToGeneral()
{
    VkCommandBuffer commandBuffer =
        m_CommandPool->BeginOneTimeCommands(m_Device);

    for(uint32_t cascadeIndex = 0; cascadeIndex < kMaxFFTCascades; cascadeIndex++){
        for(uint32_t frameIndex = 0; frameIndex < m_FrameCount; frameIndex++){
            GPUFFTFrameResources& frame =
                m_GPUFFTs[cascadeIndex]->GetFrameResources(frameIndex);

            frame.displacementImage->RecordTransitionToGeneral(commandBuffer);
            frame.normalAuxImage->RecordTransitionToGeneral(commandBuffer);
        }
    }

    m_CommandPool->EndOneTimeCommands(
        m_Device,
        m_Queue,
        commandBuffer
    );
}

void WSTessendorfGPU::CreateDescriptorSetLayouts()
{
    m_SpectrumSetLayout = vkp::DescriptorSetLayout::Builder(m_Device)
        .AddBinding(0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT)
        .AddBinding(1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT)
        .AddBinding(2, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT)
        .AddBinding(3, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT)
        .AddBinding(4, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT)
        .AddBinding(5, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT)
        .AddBinding(6, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT)
        .Build();

    m_StockhamSetLayout = vkp::DescriptorSetLayout::Builder(m_Device)
        .AddBinding(0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT)
        .AddBinding(1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT)
        .AddBinding(2, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT)
        .AddBinding(3, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT)
        .AddBinding(4, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT)
        .AddBinding(5, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT)
        .AddBinding(6, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT)
        .AddBinding(7, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT)
        .Build();

    m_OutputSetLayout = vkp::DescriptorSetLayout::Builder(m_Device)
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
    m_DescriptorPool = vkp::DescriptorPool::Builder(m_Device)
        .SetMaxSets(m_FrameCount * kMaxFFTCascades * 5)
        .AddPoolSize(
            VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            m_FrameCount * kMaxFFTCascades * 33
        )
        .AddPoolSize(
            VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
            m_FrameCount * kMaxFFTCascades * 2
        )
        .Build();
}

// 为所有 Cascade 层、所有飞行帧创建 Vulkan 描述符集，
// 并填充 GPU 资源结构供后续渲染使用。
// 负责创建计算着色器（Compute Shader） 使用的描述符集

// 描述符集（Descriptor Set）就是着色器与外部数据资源之间的“接线板”。 
// 它本身不存数据，而是一组指针或引用，告诉 GPU 的着色器：“你去哪里取数据、那块数据有多大、怎么读它。”
// 1. DescriptorWriter 把具体的缓冲区（Buffer）和纹理（Image）接到着色器的 binding 口上
// 2. 支持“一套接口，多种实现”的灵活切换 m_SpectrumSet m_PingToPongSet m_OutputSet
    // 你有多个描述符集：
    // m_SpectrumSet：指向初始频谱 h0、waveData 和输出打包缓冲区 ping。
    // m_PingToPongSet / m_PongToPingSet：指向 IFFT 的输入/输出对，每一级蝶形交替使用。
    // m_OutputSet：指向 IFFT 结果缓冲区和最终的位移纹理 displacementImage。
    // 这些描述符集让同一个 Compute Shader 在不同阶段操作不同的缓冲区和纹理，
    // 而你只需在命令缓冲里调用 vkCmdBindDescriptorSets 切换对应的集，无需修改着色器代码或管线状态
// 3. 与管线布局（Pipeline Layout）共同完成接口验证
// 4. 管理多帧并行下的资源独立性

// 1. 按 Cascade 层分离：频域数据不同 
//     静态资源不同：每一层的 h0（初始复振幅）、h0MinusConjugate（共轭项）、waveData（波矢量/色散）都是独立生成的，对应不同频段。
//     动态帧资源不同：每一层有自己独立的 ping-pong 缓冲区、位移图和法线辅助图。长波层的位移图分辨率可能较低（大补丁），短波层分辨率高（小补丁）。
//     参数不同：每层的 patchLength、amplitudeScale 不同。
// 2. 按飞行帧分离：CPU-GPU 并行安全
//     飞行帧数 = 3 时，CPU 可以提前准备第 N+1 帧的命令，而 GPU 仍在执行第 N 帧。
//     如果两个帧共用同一个描述符集（指向同一块 ping-pong 缓冲区），那么 CPU 为下一帧更新数据时，可能会覆盖 GPU 正在读取的当前帧数据，导致数据竞争和渲染错误。
//     因此，每个飞行帧需要独立的描述符集，指向该帧专用的 spectrumPing、spectrumPong、displacementImage、normalAuxImage。
// 3. 最终结构
//     m_SpectrumSets[Cascade][Frame]   // 频谱更新阶段
//     m_PingToPongSets[Cascade][Frame] // ping → pong 蝶形
//     m_PongToPingSets[Cascade][Frame] // pong → ping 蝶形
//     m_OutputSets[Cascade][Frame]     // 输出到纹理
void WSTessendorfGPU::CreateDescriptorSets()
{
    // 第一步：为每个 Cascade 的每类描述符集数组分配空间
    for(uint32_t cascadeIndex = 0; cascadeIndex < kMaxFFTCascades; cascadeIndex++){
        m_SpectrumSets[cascadeIndex].resize(m_FrameCount);   // 频谱更新阶段
        m_PingToPongSets[cascadeIndex].resize(m_FrameCount); // Stockham 蝶形: ping → pong
        m_PongToPingSets[cascadeIndex].resize(m_FrameCount); // Stockham 蝶形: pong → ping
        m_OutputPingSets[cascadeIndex].resize(m_FrameCount);
        m_OutputPongSets[cascadeIndex].resize(m_FrameCount); // 输出阶段：打包数据 → 纹理
    }

    // 第二步：按 Cascade 和飞行帧逐个填充描述符集
    for(uint32_t cascadeIndex = 0; cascadeIndex < kMaxFFTCascades; cascadeIndex++){
        GPUFFT2D& gpuFFT = *m_GPUFFTs[cascadeIndex];
        const GPUFFTStaticResources& staticResources = gpuFFT.GetStaticResources();

        // 构造静态资源的 descriptor buffer info（所有帧共用）
        VkDescriptorBufferInfo h0Info{};
        h0Info.buffer = *staticResources.h0Buffer;
        h0Info.offset = 0;
        h0Info.range = gpuFFT.GetComplexFieldSize();

        VkDescriptorBufferInfo h0MinusInfo{};
        h0MinusInfo.buffer = *staticResources.h0MinusConjugateBuffer;
        h0MinusInfo.offset = 0;
        h0MinusInfo.range = gpuFFT.GetComplexFieldSize();

        VkDescriptorBufferInfo waveDataInfo{};
        waveDataInfo.buffer = *staticResources.waveDataBuffer;
        waveDataInfo.offset = 0;
        waveDataInfo.range =
            static_cast<VkDeviceSize>(m_Resolution) *
            static_cast<VkDeviceSize>(m_Resolution) *
            sizeof(GPUWaveData);

        // 遍历该 Cascade 的每一个飞行帧
        for(uint32_t frameIndex = 0; frameIndex < m_FrameCount; frameIndex++){
            GPUFFTFrameResources& frame = gpuFFT.GetFrameResources(frameIndex);

            std::array<VkDescriptorBufferInfo, 4> pingInfos{};
            std::array<VkDescriptorBufferInfo, 4> pongInfos{};

            for(uint32_t i = 0; i < 4; i++){
                pingInfos[i].buffer = *frame.spectrumPing[i];
                pingInfos[i].offset = 0;
                pingInfos[i].range = gpuFFT.GetPackedFieldSize();

                pongInfos[i].buffer = *frame.spectrumPong[i];
                pongInfos[i].offset = 0;
                pongInfos[i].range = gpuFFT.GetPackedFieldSize();
            }

            // ---------- 描述符集 1：频谱更新阶段 ----------
            // 绑定 h0, h0MinusConjugate, waveData（只读）
            // 绑定 4 个 ping 缓冲区（只写，结果写入 ping）
            bool spectrumSuccess =
                vkp::DescriptorWriter(*m_SpectrumSetLayout, *m_DescriptorPool)
                    .WriteBuffer(0, &h0Info)          // binding 0: h0 初始频谱
                    .WriteBuffer(1, &h0MinusInfo)     // binding 1: h0 共轭项
                    .WriteBuffer(2, &waveDataInfo)    // binding 2: 波矢量/色散数据
                    .WriteBuffer(3, &pingInfos[0])    // binding 3: packed0（输出）
                    .WriteBuffer(4, &pingInfos[1])    // binding 4: packed1（输出）
                    .WriteBuffer(5, &pingInfos[2])    // binding 5: packed2（输出）
                    .WriteBuffer(6, &pingInfos[3])    // binding 6: packed3（输出）
                    .Build(m_SpectrumSets[cascadeIndex][frameIndex]);

            if(!spectrumSuccess){
                throw std::runtime_error("Failed to build GPU spectrum descriptor set");
            }

            // ---------- 描述符集 2：Stockham IFFT（ping → pong） ----------
            // 输入：4 个 ping 缓冲区（只读）
            // 输出：4 个 pong 缓冲区（只写）
            bool pingToPongSuccess =
                vkp::DescriptorWriter(*m_StockhamSetLayout, *m_DescriptorPool)
                    .WriteBuffer(0, &pingInfos[0])    // 输入 packed0
                    .WriteBuffer(1, &pingInfos[1])    // 输入 packed1
                    .WriteBuffer(2, &pingInfos[2])    // 输入 packed2
                    .WriteBuffer(3, &pingInfos[3])    // 输入 packed3
                    .WriteBuffer(4, &pongInfos[0])    // 输出 packed0
                    .WriteBuffer(5, &pongInfos[1])    // 输出 packed1
                    .WriteBuffer(6, &pongInfos[2])    // 输出 packed2
                    .WriteBuffer(7, &pongInfos[3])    // 输出 packed3
                    .Build(m_PingToPongSets[cascadeIndex][frameIndex]);

            if(!pingToPongSuccess){
                throw std::runtime_error("Failed to build GPU ping to pong descriptor set");
            }

            // ---------- 描述符集 3：Stockham IFFT（pong → ping） ----------
            // 输入：4 个 pong 缓冲区（只读）
            // 输出：4 个 ping 缓冲区（只写）
            bool pongToPingSuccess =
                vkp::DescriptorWriter(*m_StockhamSetLayout, *m_DescriptorPool)
                    .WriteBuffer(0, &pongInfos[0])
                    .WriteBuffer(1, &pongInfos[1])
                    .WriteBuffer(2, &pongInfos[2])
                    .WriteBuffer(3, &pongInfos[3])
                    .WriteBuffer(4, &pingInfos[0])
                    .WriteBuffer(5, &pingInfos[1])
                    .WriteBuffer(6, &pingInfos[2])
                    .WriteBuffer(7, &pingInfos[3])
                    .Build(m_PongToPingSets[cascadeIndex][frameIndex]);

            if(!pongToPingSuccess){
                throw std::runtime_error("Failed to build GPU pong to ping descriptor set");
            }

            // ---------- 描述符集 4：输出阶段（打包数据 → 纹理） ----------
            // 输入：4 个缓冲区（持有 IFFT 最终结果，通常是当前 ping 或 pong）
            // 输出：两张存储图像（位移图、法线辅助图），计算着色器通过 imageStore 写入
            VkDescriptorImageInfo displacementStorageInfo =
                frame.displacementImage->GetStorageDescriptorInfo();
            VkDescriptorImageInfo normalAuxStorageInfo =
                frame.normalAuxImage->GetStorageDescriptorInfo();

            bool outputPingSuccess =
                vkp::DescriptorWriter(*m_OutputSetLayout, *m_DescriptorPool)
                    .WriteBuffer(0, &pingInfos[0])    // 输入 packed0
                    .WriteBuffer(1, &pingInfos[1])    // 输入 packed1
                    .WriteBuffer(2, &pingInfos[2])    // 输入 packed2
                    .WriteBuffer(3, &pingInfos[3])    // 输入 packed3
                    .WriteImage(4, &displacementStorageInfo)  // 输出位移图
                    .WriteImage(5, &normalAuxStorageInfo)     // 输出法线辅助图
                    .Build(m_OutputPingSets[cascadeIndex][frameIndex]);

            if(!outputPingSuccess){
                throw std::runtime_error("Failed to build GPU output ping descriptor set");
            }

            bool outputPongSuccess =
                vkp::DescriptorWriter(*m_OutputSetLayout, *m_DescriptorPool)
                    .WriteBuffer(0, &pongInfos[0])
                    .WriteBuffer(1, &pongInfos[1])
                    .WriteBuffer(2, &pongInfos[2])
                    .WriteBuffer(3, &pongInfos[3])
                    .WriteImage(4, &displacementStorageInfo)
                    .WriteImage(5, &normalAuxStorageInfo)
                    .Build(m_OutputPongSets[cascadeIndex][frameIndex]);

            if(!outputPongSuccess){
                throw std::runtime_error("Failed to build GPU output pong descriptor set");
            }
        }
    }

    // 第三步：填充 GPU 资源描述结构，供渲染时绑定
    m_GPUResources.cascadeCount = kMaxFFTCascades;

    for(uint32_t cascadeIndex = 0; cascadeIndex < kMaxFFTCascades; cascadeIndex++){
        // 取第一个飞行帧的图像资源（所有帧共享相同的图像对象）
        GPUFFTFrameResources& frame0 =
            m_GPUFFTs[cascadeIndex]->GetFrameResources(0);

        // 以“通用布局下的采样器”形式提供，后续渲染前会通过屏障转为只读
        m_GPUResources.cascades[cascadeIndex].displacement =
            frame0.displacementImage->GetGeneralSampledDescriptorInfo(m_Sampler);
        m_GPUResources.cascades[cascadeIndex].normalAux =
            frame0.normalAuxImage->GetGeneralSampledDescriptorInfo(m_Sampler);

        m_GPUResources.cascades[cascadeIndex].patchLength = m_PatchLengths[cascadeIndex];
        m_GPUResources.cascades[cascadeIndex].resolution = m_Resolution;
        m_GPUResources.cascades[cascadeIndex].amplitudeScale = m_AmplitudeScales[cascadeIndex];
    }
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
    uint32_t cascadeIndex,
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
            m_GPUFFTs[cascadeIndex]->GetPackedFieldSize(),
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

// GPU 端 Tessendorf FFT 多级波浪模拟的核心更新函数，每帧调用一次。
// 它按顺序为每一层 Cascade 执行频谱演化、二维 IFFT、输出到纹理，并通过图像屏障正确衔接不同阶段的数据访问
void WSTessendorfGPU::UpdateGPU(
    VkCommandBuffer commandBuffer,
    float deltaTime
)
{
    m_Time += deltaTime;

    for(uint32_t cascadeIndex = 0; cascadeIndex < kMaxFFTCascades; cascadeIndex++){
        GPUFFTFrameResources& frame =
            m_GPUFFTs[cascadeIndex]->GetFrameResources(m_FrameIndex);

        // 图形读取 → 计算写入的布局转换（RecordGraphicsReadToComputeWriteBarrier）
        frame.displacementImage->RecordGraphicsReadToComputeWriteBarrier(commandBuffer);
        frame.normalAuxImage->RecordGraphicsReadToComputeWriteBarrier(commandBuffer);

        // 频谱更新（RecordSpectrumUpdate）
        RecordSpectrumUpdate(commandBuffer, cascadeIndex);

        // 屏障：频谱写入 → IFFT 读取（RecordPackedBarrier）
        RecordPackedBarrier(
            commandBuffer,
            cascadeIndex,
            frame,
            true,
            VK_ACCESS_SHADER_WRITE_BIT,
            VK_ACCESS_SHADER_READ_BIT,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT
        );

        bool currentPing = true;

        // 二维 IFFT：行变换 + 列变换（RecordRowFFT / RecordColumnFFT）
        currentPing = RecordRowFFT(
            commandBuffer,
            cascadeIndex,
            currentPing
        );

        currentPing = RecordColumnFFT(
            commandBuffer,
            cascadeIndex,
            currentPing
        );

        // 输出：打包缓冲区 → 纹理（RecordOutput）
        RecordOutput(
            commandBuffer,
            cascadeIndex,
            currentPing
        );

        // 计算写入 → 图形读取的布局转换（RecordComputeWriteToGraphicsReadBarrier）
        frame.displacementImage->RecordComputeWriteToGraphicsReadBarrier(commandBuffer);
        frame.normalAuxImage->RecordComputeWriteToGraphicsReadBarrier(commandBuffer);
    }
}

void WSTessendorfGPU::RecordSpectrumUpdate(
    VkCommandBuffer commandBuffer,
    uint32_t cascadeIndex
)
{
    vkCmdBindPipeline(
        commandBuffer,
        VK_PIPELINE_BIND_POINT_COMPUTE,
        *m_SpectrumPipeline
    );

    VkDescriptorSet descriptorSet =
        m_SpectrumSets[cascadeIndex][m_FrameIndex];

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

bool WSTessendorfGPU::RecordRowFFT(
    VkCommandBuffer commandBuffer,
    uint32_t cascadeIndex,
    bool inputIsPing
)
{
    GPUFFTFrameResources& frame =
        m_GPUFFTs[cascadeIndex]->GetFrameResources(m_FrameIndex);

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

    bool currentPing = inputIsPing;

    for(uint32_t stage = 0; stage < logN; stage++){
        VkDescriptorSet descriptorSet =
            currentPing
                ? m_PingToPongSets[cascadeIndex][m_FrameIndex]
                : m_PongToPingSets[cascadeIndex][m_FrameIndex];

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
            cascadeIndex,
            frame,
            outputIsPing,
            VK_ACCESS_SHADER_WRITE_BIT,
            VK_ACCESS_SHADER_READ_BIT,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT
        );

        currentPing = !currentPing;
    }

    return currentPing;
}

bool WSTessendorfGPU::RecordColumnFFT(
    VkCommandBuffer commandBuffer,
    uint32_t cascadeIndex,
    bool inputIsPing
)
{
    GPUFFTFrameResources& frame =
        m_GPUFFTs[cascadeIndex]->GetFrameResources(m_FrameIndex);

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

    bool currentPing = inputIsPing;

    for(uint32_t stage = 0; stage < logN; stage++){
        VkDescriptorSet descriptorSet =
            currentPing
                ? m_PingToPongSets[cascadeIndex][m_FrameIndex]
                : m_PongToPingSets[cascadeIndex][m_FrameIndex];

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
            cascadeIndex,
            frame,
            outputIsPing,
            VK_ACCESS_SHADER_WRITE_BIT,
            VK_ACCESS_SHADER_READ_BIT,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT
        );

        currentPing = !currentPing;
    }

    return currentPing;
}

void WSTessendorfGPU::RecordOutput(
    VkCommandBuffer commandBuffer,
    uint32_t cascadeIndex,
    bool inputIsPing
)
{
    vkCmdBindPipeline(
        commandBuffer,
        VK_PIPELINE_BIND_POINT_COMPUTE,
        *m_OutputPipeline
    );

    VkDescriptorSet descriptorSet = inputIsPing
        ? m_OutputPingSets[cascadeIndex][m_FrameIndex]
        : m_OutputPongSets[cascadeIndex][m_FrameIndex];

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