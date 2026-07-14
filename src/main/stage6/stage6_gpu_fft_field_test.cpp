#include "core/Application.h"

#include "scene/water/gpu/ComputePipeline.h"
#include "scene/water/gpu/GPUFFT2D.h"
#include "scene/water/sources/WSTessendorfCPU.h"

#include "vulkan/Descriptors.h"

#include <GLFW/glfw3.h>
#include <glm/glm.hpp>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <exception>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <vector>

// CPU: WSTessendorfCPU 生成参考数据 (高度场等)
// GPU:
//   1. 频谱更新 shader → 输出打包频谱到 ping
//   2. for direction in {行, 列}:
//        for stage in 0..log2(N)-1:
//            Stockham shader: ping → pong 或 pong → ping
//   3. 将最终结果拷贝到 host-visible 回读缓冲区
// CPU:
//   回读缓冲区数据 → 提取高度 → 与 CPU 参考对比 → 输出误差
namespace
{
// 传递给频谱更新 Compute Shader 的 push constant 参数（分辨率、时间）
struct SpectrumPushConstants
{
    uint32_t resolution = 0;
    float time = 0.0f;
};

// 传递给 Stockham IFFT Compute Shader 的 push constant 参数（分辨率、当前级、正/逆变换标志）
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

// 存储 GPU 高度与 CPU 参考高度之间的误差统计（最大误差、平均误差、相对 RMS 误差、NaN/Inf 计数）
struct ErrorStats
{
    float maxAbsError = 0.0f;
    float meanAbsError = 0.0f;
    float relativeRmsError = 0.0f;
    uint32_t nanCount = 0;
    uint32_t infCount = 0;
};

// 计算 log2(N)，用于确定 FFT 的总级数（例如 N=64 返回 6）
uint32_t Log2(uint32_t value)
{
    uint32_t result = 0;

    while(value > 1){
        value >>= 1;
        result++;
    }

    return result;
}

// 逐元素比较 GPU 高度与 CPU 参考高度，计算 ErrorStats。跳过 NaN 和 Inf 值，避免污染统计
ErrorStats CompareHeight(
    const std::vector<float>& gpu,
    const std::vector<glm::vec4>& cpuDisplacement
)
{
    if(gpu.size() != cpuDisplacement.size()){
        throw std::runtime_error("CompareHeight size mismatch");
    }

    ErrorStats stats{};

    double absErrorSum = 0.0;
    double squaredErrorSum = 0.0;
    double referenceSquaredSum = 0.0;

    for(size_t i = 0; i < gpu.size(); i++){
        float gpuValue = gpu[i];
        float cpuValue = cpuDisplacement[i].y;

        if(std::isnan(gpuValue) || std::isnan(cpuValue)){
            stats.nanCount++;
            continue;
        }

        if(std::isinf(gpuValue) || std::isinf(cpuValue)){
            stats.infCount++;
            continue;
        }

        float error = std::abs(gpuValue - cpuValue);

        stats.maxAbsError = std::max(stats.maxAbsError, error);

        absErrorSum += error;
        squaredErrorSum += static_cast<double>(error) * static_cast<double>(error);
        referenceSquaredSum += static_cast<double>(cpuValue) * static_cast<double>(cpuValue);
    }

    double validCount =
        static_cast<double>(gpu.size() - stats.nanCount - stats.infCount);

    if(validCount > 0.0){
        stats.meanAbsError =
            static_cast<float>(absErrorSum / validCount);
    }

    if(referenceSquaredSum > 1e-20){
        stats.relativeRmsError =
            static_cast<float>(std::sqrt(squaredErrorSum / referenceSquaredSum));
    }

    return stats;
}

ErrorStats CompareVec4Fields(
    const glm::vec4* gpu,
    const std::vector<glm::vec4>& cpu
)
{
    ErrorStats stats{};

    double absErrorSum = 0.0;
    double squaredErrorSum = 0.0;
    double referenceSquaredSum = 0.0;
    uint32_t componentCount = 0;

    for(size_t i = 0; i < cpu.size(); i++){
        for(uint32_t c = 0; c < 4; c++){
            float gpuValue = gpu[i][c];
            float cpuValue = cpu[i][c];

            if(std::isnan(gpuValue) || std::isnan(cpuValue)){
                stats.nanCount++;
                continue;
            }

            if(std::isinf(gpuValue) || std::isinf(cpuValue)){
                stats.infCount++;
                continue;
            }

            float error = std::abs(gpuValue - cpuValue);

            stats.maxAbsError = std::max(stats.maxAbsError, error);

            absErrorSum += error;
            squaredErrorSum +=
                static_cast<double>(error) *
                static_cast<double>(error);

            referenceSquaredSum +=
                static_cast<double>(cpuValue) *
                static_cast<double>(cpuValue);

            componentCount++;
        }
    }

    if(componentCount > 0){
        stats.meanAbsError =
            static_cast<float>(
                absErrorSum /
                static_cast<double>(componentCount)
            );
    }

    if(referenceSquaredSum > 1e-20){
        stats.relativeRmsError =
            static_cast<float>(
                std::sqrt(squaredErrorSum / referenceSquaredSum)
            );
    }

    return stats;
}

// 将 ErrorStats 格式化输出到控制台
void PrintStats(const char* label, const ErrorStats& stats)
{
    std::cout
        << label << "\n"
        << "  maxAbsError = " << stats.maxAbsError << "\n"
        << "  meanAbsError = " << stats.meanAbsError << "\n"
        << "  relativeRmsError = " << stats.relativeRmsError << "\n"
        << "  nanCount = " << stats.nanCount << "\n"
        << "  infCount = " << stats.infCount << "\n";
}
}

class GPUFFTHeightTestApp : public core::Application
{
protected:
    void Start() override;
    void Update(core::Timestep timestep) override;
    void Render(VkCommandBuffer commandBuffer, uint32_t imageIndex) override;
    void ShutdownApp() override;

private:
    void CreateCPUReference();
    void CreateGPUResources();
    void CreateDescriptorSetLayouts();
    void CreatePipelines();
    void CreateDescriptorPool();
    void CreateDescriptorSets();
    void CreateReadbackBuffer(); // 用于从 GPU 读取结果

    void RecordBufferBarrier(
        VkCommandBuffer commandBuffer,
        VkBuffer buffer,
        VkDeviceSize size,
        VkAccessFlags srcAccess,
        VkAccessFlags dstAccess,
        VkPipelineStageFlags srcStage,
        VkPipelineStageFlags dstStage
    ); // 用于记录缓冲区的访问屏障

    void RecordPackedBarrier(
        VkCommandBuffer commandBuffer,
        water::GPUFFTFrameResources& resources,
        bool ping,
        VkAccessFlags srcAccess,
        VkAccessFlags dstAccess,
        VkPipelineStageFlags srcStage,
        VkPipelineStageFlags dstStage
    ); // 用于记录打包缓冲区的访问屏障

    void RunValidation();
    void CompareReadback(); // 用于比较 GPU 读取的结果和 CPU 计算的结果

private:
    uint32_t m_Resolution = 64;
    float m_TestTime = 1.25f;

    std::unique_ptr<water::WSTessendorfCPU> m_CPUReference; // CPU 参考实现
    std::unique_ptr<water::GPUFFT2D> m_GPUFFT; // GPU FFT 实现

    std::unique_ptr<water::ComputePipeline> m_SpectrumPipeline;
    std::unique_ptr<water::ComputePipeline> m_StockhamPipeline;

    std::unique_ptr<vkp::DescriptorSetLayout> m_SpectrumSetLayout;
    std::unique_ptr<vkp::DescriptorSetLayout> m_StockhamSetLayout;
    std::unique_ptr<vkp::DescriptorPool> m_DescriptorPool;

    VkDescriptorSet m_SpectrumSet = VK_NULL_HANDLE;
    VkDescriptorSet m_PingToPongSet = VK_NULL_HANDLE;
    VkDescriptorSet m_PongToPingSet = VK_NULL_HANDLE;

    std::unique_ptr<vkp::Buffer> m_ReadbackBuffer;

    // 专用 Compute Shader，负责将 IFFT 输出的打包缓冲区内容写入 DynamicImage2D 纹理（位移图和法线辅助图）
    std::unique_ptr<water::ComputePipeline> m_OutputPipeline;
    // 为输出阶段的 Compute Shader 提供描述符布局和描述符集，
    // 绑定输入缓冲区（ping/pong 结果）和输出纹理（位移图、法线辅助图）
    std::unique_ptr<vkp::DescriptorSetLayout> m_OutputSetLayout;
    VkDescriptorSet m_OutputSet = VK_NULL_HANDLE;

    // Host-visible 回读缓冲区，用于从位移纹理和法线辅助纹理中拷贝数据回 CPU，逐元素与 CPU 参考值对比
    std::unique_ptr<vkp::Buffer> m_DisplacementReadbackBuffer;
    std::unique_ptr<vkp::Buffer> m_NormalAuxReadbackBuffer;
};

void GPUFFTHeightTestApp::Start()
{
    CreateCPUReference();
    CreateGPUResources();
    CreateDescriptorSetLayouts();
    CreatePipelines();
    CreateDescriptorPool();
    CreateDescriptorSets();
    CreateReadbackBuffer();

    RunValidation();
    CompareReadback();

    glfwSetWindowShouldClose(
        GetWindow().GetNativeWindow(),
        GLFW_TRUE
    );
}

// 空实现，本测试只做一次性验证，无需逐帧更新
void GPUFFTHeightTestApp::Update(core::Timestep timestep)
{
}

// 空实现，录制一个空的命令缓冲区（本测试不通过图形管线渲染）
void GPUFFTHeightTestApp::Render(VkCommandBuffer commandBuffer, uint32_t imageIndex)
{
    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;

    vkBeginCommandBuffer(commandBuffer, &beginInfo);
    vkEndCommandBuffer(commandBuffer);
}

// 清理所有资源（逆序销毁），确保无 Vulkan 对象泄漏
void GPUFFTHeightTestApp::ShutdownApp()
{
    m_NormalAuxReadbackBuffer.reset();
    m_DisplacementReadbackBuffer.reset();
    m_ReadbackBuffer.reset();

    m_DescriptorPool.reset();
    m_OutputSetLayout.reset();
    m_StockhamSetLayout.reset();
    m_SpectrumSetLayout.reset();

    m_OutputPipeline.reset();
    m_StockhamPipeline.reset();
    m_SpectrumPipeline.reset();

    m_GPUFFT.reset();
    m_CPUReference.reset();
}

// 创建 WSTessendorfCPU 实例，使用固定频谱参数（分辨率 64，风速 25 m/s，choppy=1.0 等），
// 并立即计算 t=1.25 时刻的波浪数据作为参考标准
void GPUFFTHeightTestApp::CreateCPUReference()
{
    water::TessendorfSpectrumParams params{};
    params.resolution = m_Resolution;
    params.patchLength = 256.0f;
    params.windDirection = glm::normalize(glm::vec2(1.0f, 0.25f));
    params.windSpeed = 25.0f;
    params.spectrumAmplitude = 2.5f;
    params.shortWaveDamping = 0.001f;
    params.gravity = 9.81f;
    params.choppyLambda = 1.0f;
    params.oppositeWindDamping = 0.07f;
    params.randomSeed = 1337;

    m_CPUReference = std::make_unique<water::WSTessendorfCPU>(params);
    m_CPUReference->ComputeAtTime(m_TestTime);
}

// 创建 GPUFFT2D 实例。它从 m_CPUReference 提取初始频谱数据（h0, h0MinusConjugate, 波矢量数组）
// 并上传到 GPU 设备本地缓冲区，同时分配 ping-pong 计算缓冲区和输出纹理
void GPUFFTHeightTestApp::CreateGPUResources()
{
    m_GPUFFT = std::make_unique<water::GPUFFT2D>(
        GetPhysicalDevice(),
        GetDevice(),
        GetCommandPool(),
        GetDevice().GetGraphicsQueue(),
        m_Resolution,
        1,
        *m_CPUReference
    );
}

// 创建两组描述符集布局：m_SpectrumSetLayout（7 个 storage buffer 用于频谱更新阶段），
// m_StockhamSetLayout（8 个 storage buffer 用于 Stockham IFFT 的 ping-pong 读写）
void GPUFFTHeightTestApp::CreateDescriptorSetLayouts()
{
    m_SpectrumSetLayout = vkp::DescriptorSetLayout::Builder(GetDevice())
        .AddBinding(0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT)
        .AddBinding(1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT)
        .AddBinding(2, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT)
        .AddBinding(3, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT)
        .AddBinding(4, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT)
        .AddBinding(5, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT)
        .AddBinding(6, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT)
        .Build();

    m_StockhamSetLayout = vkp::DescriptorSetLayout::Builder(GetDevice())
        .AddBinding(0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT)
        .AddBinding(1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT)
        .AddBinding(2, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT)
        .AddBinding(3, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT)
        .AddBinding(4, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT)
        .AddBinding(5, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT)
        .AddBinding(6, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT)
        .AddBinding(7, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT)
        .Build();

    m_OutputSetLayout = vkp::DescriptorSetLayout::Builder(GetDevice())
        .AddBinding(0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT)
        .AddBinding(1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT)
        .AddBinding(2, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT)
        .AddBinding(3, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT)
        .AddBinding(4, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_SHADER_STAGE_COMPUTE_BIT)
        .AddBinding(5, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_SHADER_STAGE_COMPUTE_BIT)
        .Build();
}

// 创建两个 Compute Pipeline：m_SpectrumPipeline（频谱演化着色器）和
// m_StockhamPipeline（Stockham IFFT 着色器），配置 push constant 大小与描述符集布局
void GPUFFTHeightTestApp::CreatePipelines()
{
    water::ComputePipelineConfig spectrumConfig{};
    spectrumConfig.descriptorSetLayouts = {*m_SpectrumSetLayout};
    spectrumConfig.enablePushConstants = true;
    spectrumConfig.pushConstantSize = sizeof(SpectrumPushConstants);

    m_SpectrumPipeline = std::make_unique<water::ComputePipeline>(
        GetDevice(),
        "shaders/water/fft/spectrum_update.comp.spv",
        spectrumConfig
    );

    water::ComputePipelineConfig stockhamConfig{};
    stockhamConfig.descriptorSetLayouts = {*m_StockhamSetLayout};
    stockhamConfig.enablePushConstants = true;
    stockhamConfig.pushConstantSize = sizeof(FFTPushConstants);

    m_StockhamPipeline = std::make_unique<water::ComputePipeline>(
        GetDevice(),
        "shaders/water/fft/fft_stockham.comp.spv",
        stockhamConfig
    );

    water::ComputePipelineConfig outputConfig{};
    outputConfig.descriptorSetLayouts = {*m_OutputSetLayout};
    outputConfig.enablePushConstants = true;
    outputConfig.pushConstantSize = sizeof(OutputPushConstants);

    m_OutputPipeline = std::make_unique<water::ComputePipeline>(
        GetDevice(),
        "shaders/water/fft/fft_output.comp.spv",
        outputConfig
    );
}

// spectrum set = 7 buffers
// ping→pong set = 8 buffers
// pong→ping set = 8 buffers
// 创建描述符池，最大 3 个描述符集，共预分配 23 个 storage buffer 描述符位
void GPUFFTHeightTestApp::CreateDescriptorPool()
{
    m_DescriptorPool = vkp::DescriptorPool::Builder(GetDevice())
        .SetMaxSets(4)
        .AddPoolSize(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 27)
        .AddPoolSize(VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 2)
        .Build();
}

// 创建并填充三个描述符集：
// • m_SpectrumSet：绑定静态资源（h0, h0MinusConj, waveData）和 4 个 ping 缓冲区
// • m_PingToPongSet：绑定 ping 为输入、pong 为输出
// • m_PongToPingSet：绑定 pong 为输入、ping 为输出
//  IFFT 需要执行 log2N 级蝶形运算，每一级都需要读取上一级的输出，并将本级结果写入另一个缓冲区，以避免数据覆盖
// 第 0 级 IFFT 绑定 m_PingToPongSet（ping 输入 → pong 输出）
// 第 1 级 IFFT 绑定 m_PongToPingSet（pong 输入 → ping 输出）
// 如果只用一个描述符集，每次切换级时就需要用 vkUpdateDescriptorSets 动态修改 buffer binding，这会引入额外的 GPU 同步和 CPU 开销
void GPUFFTHeightTestApp::CreateDescriptorSets()
{
    water::GPUFFTFrameResources& frame =
        m_GPUFFT->GetFrameResources(0);

    const water::GPUFFTStaticResources& staticResources =
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
        sizeof(water::GPUWaveData);

    std::array<VkDescriptorBufferInfo, 4> pingInfos{};

    for(uint32_t i = 0; i < 4; i++){
        pingInfos[i].buffer = *frame.spectrumPing[i];
        pingInfos[i].offset = 0;
        pingInfos[i].range = m_GPUFFT->GetPackedFieldSize();
    }

    bool spectrumSuccess =
        vkp::DescriptorWriter(*m_SpectrumSetLayout, *m_DescriptorPool)
            .WriteBuffer(0, &h0Info)
            .WriteBuffer(1, &h0MinusInfo)
            .WriteBuffer(2, &waveDataInfo)
            .WriteBuffer(3, &pingInfos[0])
            .WriteBuffer(4, &pingInfos[1])
            .WriteBuffer(5, &pingInfos[2])
            .WriteBuffer(6, &pingInfos[3])
            .Build(m_SpectrumSet);

    if(!spectrumSuccess){
        throw std::runtime_error("Failed to build spectrum descriptor set");
    }

        std::array<VkDescriptorBufferInfo, 4> pongInfos{};

    for(uint32_t i = 0; i < 4; i++){
        pongInfos[i].buffer = *frame.spectrumPong[i];
        pongInfos[i].offset = 0;
        pongInfos[i].range = m_GPUFFT->GetPackedFieldSize();
    }

    bool pingToPongSuccess =
        vkp::DescriptorWriter(*m_StockhamSetLayout, *m_DescriptorPool)
            .WriteBuffer(0, &pingInfos[0])
            .WriteBuffer(1, &pingInfos[1])
            .WriteBuffer(2, &pingInfos[2])
            .WriteBuffer(3, &pingInfos[3])
            .WriteBuffer(4, &pongInfos[0])
            .WriteBuffer(5, &pongInfos[1])
            .WriteBuffer(6, &pongInfos[2])
            .WriteBuffer(7, &pongInfos[3])
            .Build(m_PingToPongSet);

    if(!pingToPongSuccess){
        throw std::runtime_error("Failed to build ping to pong descriptor set");
    }

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
            .Build(m_PongToPingSet);

    if(!pongToPingSuccess){
        throw std::runtime_error("Failed to build pong to ping descriptor set");
    }

    VkDescriptorImageInfo displacementStorageInfo =
        frame.displacementImage->GetStorageDescriptorInfo();

    VkDescriptorImageInfo normalAuxStorageInfo =
        frame.normalAuxImage->GetStorageDescriptorInfo();

    bool outputSuccess =
        vkp::DescriptorWriter(*m_OutputSetLayout, *m_DescriptorPool)
            .WriteBuffer(0, &pingInfos[0])
            .WriteBuffer(1, &pingInfos[1])
            .WriteBuffer(2, &pingInfos[2])
            .WriteBuffer(3, &pingInfos[3])
            .WriteImage(4, &displacementStorageInfo)
            .WriteImage(5, &normalAuxStorageInfo)
            .Build(m_OutputSet);

    if(!outputSuccess){
        throw std::runtime_error("Failed to build output descriptor set");
    }
}

// 创建一个 host-visible 缓冲区，用于从 GPU 读取最终 IFFT 结果，并持久映射
void GPUFFTHeightTestApp::CreateReadbackBuffer()
{
    m_ReadbackBuffer = std::make_unique<vkp::Buffer>(
        GetPhysicalDevice(),
        GetDevice(),
        m_GPUFFT->GetPackedFieldSize(),
        VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
            VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
    );

    m_ReadbackBuffer->Map();

    VkDeviceSize imageReadbackSize =
        static_cast<VkDeviceSize>(m_Resolution) *
        static_cast<VkDeviceSize>(m_Resolution) *
        sizeof(glm::vec4);

    m_DisplacementReadbackBuffer = std::make_unique<vkp::Buffer>(
        GetPhysicalDevice(),
        GetDevice(),
        imageReadbackSize,
        VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
            VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
    );

    m_DisplacementReadbackBuffer->Map();

    m_NormalAuxReadbackBuffer = std::make_unique<vkp::Buffer>(
        GetPhysicalDevice(),
        GetDevice(),
        imageReadbackSize,
        VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
            VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
    );

    m_NormalAuxReadbackBuffer->Map();
}

// 录制单个缓冲区的管线屏障，控制缓冲区在 compute shader 写入与后续传输/读取之间的访问同步
void GPUFFTHeightTestApp::RecordBufferBarrier(
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

// 批量录制 4 个 ping 或 pong 缓冲区的管线屏障
void GPUFFTHeightTestApp::RecordPackedBarrier(
    VkCommandBuffer commandBuffer,
    water::GPUFFTFrameResources& resources,
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

// 核心验证流程：录制一次性命令缓冲区，依次执行：
// 1. 频谱更新 Compute Shader（输出到 ping 缓冲区）
// 2. 为每个方向（行/列）的每一级 Stockham IFFT 调度 Compute Shader，在 ping 和 pong 之间交替
// 3. 将最终结果从最终缓冲区（ping 或 pong）拷贝到回读缓冲区
void GPUFFTHeightTestApp::RunValidation()
{
    water::GPUFFTFrameResources& frame =
        m_GPUFFT->GetFrameResources(0);
    
    VkCommandBuffer commandBuffer =
        GetCommandPool().BeginOneTimeCommands(GetDevice());

    frame.displacementImage->RecordTransitionToGeneral(commandBuffer);
    frame.normalAuxImage->RecordTransitionToGeneral(commandBuffer);

    vkCmdBindPipeline(
        commandBuffer,
        VK_PIPELINE_BIND_POINT_COMPUTE,
        *m_SpectrumPipeline
    );

    vkCmdBindDescriptorSets(
        commandBuffer,
        VK_PIPELINE_BIND_POINT_COMPUTE,
        m_SpectrumPipeline->GetLayout(),
        0,
        1,
        &m_SpectrumSet,
        0,
        nullptr
    );

    SpectrumPushConstants spectrumPush{};
    spectrumPush.resolution = m_Resolution;
    spectrumPush.time = m_TestTime;

    vkCmdPushConstants(
        commandBuffer,
        m_SpectrumPipeline->GetLayout(),
        VK_SHADER_STAGE_COMPUTE_BIT,
        0,
        sizeof(SpectrumPushConstants),
        &spectrumPush
    );

    uint32_t total =
        m_Resolution *
        m_Resolution;

    uint32_t groupCount =
        (total + 63) / 64;

    vkCmdDispatch(commandBuffer, groupCount, 1, 1);

    RecordPackedBarrier(
        commandBuffer,
        frame,
        true,
        VK_ACCESS_SHADER_WRITE_BIT,
        VK_ACCESS_SHADER_READ_BIT,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT
    );

    vkCmdBindPipeline(
        commandBuffer,
        VK_PIPELINE_BIND_POINT_COMPUTE,
        *m_StockhamPipeline
    );

    uint32_t logN = Log2(m_Resolution);
    uint32_t butterflyCount =
        (m_Resolution * m_Resolution) / 2;

    uint32_t butterflyGroupCount =
        (butterflyCount + 63) / 64;

    bool currentPing = true;

    for(uint32_t direction = 0; direction < 2; direction++){
        for(uint32_t stage = 0; stage < logN; stage++){
            VkDescriptorSet descriptorSet =
                currentPing ? m_PingToPongSet : m_PongToPingSet;

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

            FFTPushConstants fftPush{};
            fftPush.resolution = m_Resolution;
            fftPush.stage = stage;
            fftPush.direction = direction;
            fftPush.inverse = 1;

            vkCmdPushConstants(
                commandBuffer,
                m_StockhamPipeline->GetLayout(),
                VK_SHADER_STAGE_COMPUTE_BIT,
                0,
                sizeof(FFTPushConstants),
                &fftPush
            );

            vkCmdDispatch(commandBuffer, butterflyGroupCount, 1, 1);

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

    vkCmdBindPipeline(
        commandBuffer,
        VK_PIPELINE_BIND_POINT_COMPUTE,
        *m_OutputPipeline
    );

    vkCmdBindDescriptorSets(
        commandBuffer,
        VK_PIPELINE_BIND_POINT_COMPUTE,
        m_OutputPipeline->GetLayout(),
        0,
        1,
        &m_OutputSet,
        0,
        nullptr
    );

    OutputPushConstants outputPush{};
    outputPush.resolution = m_Resolution;
    outputPush.choppyLambda = 1.0f;

    vkCmdPushConstants(
        commandBuffer,
        m_OutputPipeline->GetLayout(),
        VK_SHADER_STAGE_COMPUTE_BIT,
        0,
        sizeof(OutputPushConstants),
        &outputPush
    );

    vkCmdDispatch(commandBuffer, groupCount, 1, 1);

    frame.displacementImage->RecordComputeWriteToTransferReadBarrier(commandBuffer);
    frame.normalAuxImage->RecordComputeWriteToTransferReadBarrier(commandBuffer);

    VkBufferImageCopy imageCopy{};
    imageCopy.bufferOffset = 0;
    imageCopy.bufferRowLength = 0;
    imageCopy.bufferImageHeight = 0;
    imageCopy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    imageCopy.imageSubresource.mipLevel = 0;
    imageCopy.imageSubresource.baseArrayLayer = 0;
    imageCopy.imageSubresource.layerCount = 1;
    imageCopy.imageOffset = {0, 0, 0};
    imageCopy.imageExtent = {m_Resolution, m_Resolution, 1};

    vkCmdCopyImageToBuffer(
        commandBuffer,
        frame.displacementImage->GetImage(),
        VK_IMAGE_LAYOUT_GENERAL,
        *m_DisplacementReadbackBuffer,
        1,
        &imageCopy
    );

    vkCmdCopyImageToBuffer(
        commandBuffer,
        frame.normalAuxImage->GetImage(),
        VK_IMAGE_LAYOUT_GENERAL,
        *m_NormalAuxReadbackBuffer,
        1,
        &imageCopy
    );

    bool finalIsPing = currentPing;

    VkBuffer finalPacked0 = finalIsPing
        ? *frame.spectrumPing[0]
        : *frame.spectrumPong[0];

    RecordBufferBarrier(
        commandBuffer,
        finalPacked0,
        m_GPUFFT->GetPackedFieldSize(),
        VK_ACCESS_SHADER_WRITE_BIT,
        VK_ACCESS_TRANSFER_READ_BIT,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        VK_PIPELINE_STAGE_TRANSFER_BIT
    );

    VkBufferCopy copyRegion{};
    copyRegion.srcOffset = 0;
    copyRegion.dstOffset = 0;
    copyRegion.size = m_GPUFFT->GetPackedFieldSize();

    vkCmdCopyBuffer(
        commandBuffer,
        finalPacked0,
        *m_ReadbackBuffer,
        1,
        &copyRegion
    );

    GetCommandPool().EndOneTimeCommands(
        GetDevice(),
        GetDevice().GetGraphicsQueue(),
        commandBuffer
    );
}

// 从回读缓冲区提取 GPU 计算的高度场，与 CPU 参考实现的高度场做逐元素对比，输出误差统计。
// 如果 NaN/Inf 计数非零或相对 RMS 误差 > 1e-3，则抛出异常
void GPUFFTHeightTestApp::CompareReadback()
{
    void* mapped = m_ReadbackBuffer->GetMappedData();

    if(mapped == nullptr){
        throw std::runtime_error("Readback buffer is not mapped");
    }

    size_t count =
        static_cast<size_t>(m_Resolution) *
        static_cast<size_t>(m_Resolution);

    const glm::vec4* packed =
        static_cast<const glm::vec4*>(mapped);

    std::vector<float> gpuHeight(count);

    float normalization =
        1.0f / static_cast<float>(count);

    for(size_t i = 0; i < count; i++){
        gpuHeight[i] = packed[i].x * normalization;
    }

    const water::CPUWaterSurfaceFrame& cpuFrame =
        m_CPUReference->GetFrame();

    ErrorStats stats =
        CompareHeight(gpuHeight, cpuFrame.displacement);

    PrintStats("GPU height vs CPU FFT", stats);

    if(stats.nanCount != 0 || stats.infCount != 0){
        throw std::runtime_error("GPU FFT height contains NaN or Inf");
    }

    if(stats.relativeRmsError > 1e-3f){
        throw std::runtime_error("GPU FFT height relative RMS error too high");
    }

    std::cout << "GPU height validation passed\n";

    const glm::vec4* gpuDisplacement =
        static_cast<const glm::vec4*>(
            m_DisplacementReadbackBuffer->GetMappedData()
        );

    const glm::vec4* gpuNormalAux =
        static_cast<const glm::vec4*>(
            m_NormalAuxReadbackBuffer->GetMappedData()
        );

    if(gpuDisplacement == nullptr){
        throw std::runtime_error("Displacement readback buffer is not mapped");
    }

    if(gpuNormalAux == nullptr){
        throw std::runtime_error("NormalAux readback buffer is not mapped");
    }

    ErrorStats displacementStats =
        CompareVec4Fields(
            gpuDisplacement,
            cpuFrame.displacement
        );

    ErrorStats normalAuxStats =
        CompareVec4Fields(
            gpuNormalAux,
            cpuFrame.normalAux
        );

    PrintStats("GPU displacement image vs CPU", displacementStats);
    PrintStats("GPU normalAux image vs CPU", normalAuxStats);

    if(displacementStats.nanCount != 0 || displacementStats.infCount != 0){
        throw std::runtime_error("GPU displacement contains NaN or Inf");
    }

    if(normalAuxStats.nanCount != 0 || normalAuxStats.infCount != 0){
        throw std::runtime_error("GPU normalAux contains NaN or Inf");
    }

    if(displacementStats.relativeRmsError > 1e-3f){
        throw std::runtime_error("GPU displacement relative RMS error too high");
    }

    if(normalAuxStats.relativeRmsError > 1e-3f){
        throw std::runtime_error("GPU normalAux relative RMS error too high");
    }

    std::cout << "GPU full field validation passed\n";
}

int main()
{
    try{
        GPUFFTHeightTestApp app;
        app.Run();
    }
    catch(const std::exception& e){
        std::cerr << e.what() << "\n";
        return 1;
    }

    return 0;
}