#include "main/stage7/Stage7BoreFrontApp.h"

#include "core/Log.h"

#include "scene/water/render/WaterVertex.h"

#include <GLFW/glfw3.h>
#include <glm/gtc/constants.hpp>

#include <array>
#include <cmath>
#include <sstream>
#include <stdexcept>
#include <iostream>
#include <algorithm>

// Start()
// ├── VKP_INFO("Stage7BoreFrontApp started")
// ├── static_assert(sizeof(glm::vec4) == 16)
// ├── m_Camera.AddYawPitch(0.0f, -300.0f)               // 初始俯仰角
// ├── CreateDescriptorSetLayout()                       // 图形管线描述符布局：2 UBO + 6 纹理（3层位移+法线辅助）
// ├── CreatePipelines()                                 // 实体 & 线框图形管线（共用布局）
// ├── CreateWaterGrid()                                 // 静态 DEVICE_LOCAL 水面网格
// ├── CreateSamplers()                                  // 位移图采样器（NEAREST / LINEAR）
// ├── CreateGPUFFTSource()                              // 初始化 WSTessendorfGPU：
// │   ├── 创建 3 层 GPUFFT2D，每层独立静态资源（h0, h0MinusConj, waveData）和帧资源（ping-pong buffer, 图像）
// │   ├── 创建 Compute Pipeline（频谱初始化、频谱演化、Stockham IFFT、输出到纹理）
// │   ├── 为每层每帧创建描述符集（spectrum/ping-pong/output）
// │   └── 构建 GPUResources（每帧每层的 patch/振幅/图像信息）
// ├── CreateUniformBuffers()                            // CameraUBO + WaterParamsUBO（每飞行帧一个，HOST_VISIBLE）
// ├── CreateDescriptorPool()                            // 池子大小：UBO × 2N + COMBINED_IMAGE_SAMPLER × 6N
// └── CreateDescriptorSets()                            // 为每个飞行帧绑定：
//     ├── binding 0: CameraUBO
//     ├── binding 1: WaterParamsUBO
//     ├── binding 2‑3: Cascade 0 位移 & 法线辅助
//     ├── binding 4‑5: Cascade 1 位移 & 法线辅助
//     └── binding 6‑7: Cascade 2 位移 & 法线辅助

// 每帧运行流程（Application 主循环）：
// Loop()
// └── for each frame
//     ├── Update(timestep)                              // 更新相机、累计模拟时间
//     ├── PrepareFrame(frameIndex, imageIndex)           // 更新 Camera UBO 和 WaterParams UBO
//     └── Render(commandBuffer, imageIndex)              // 录制命令：
//         ├── m_TessendorfGPU->UpdateGPU(commandBuffer, currentFrame, dt)
//         │   └── 为每层 Cascade 依次：
//         │       ├── 图像屏障：GRAPHICS_READ → COMPUTE_WRITE
//         │       ├── 频谱更新 Compute Shader（h0 × e^{iωt} 写入 ping）
//         │       ├── 屏障：COMPUTE_WRITE → COMPUTE_READ
//         │       ├── Stockham 行/列 IFFT（ping-pong 交替）
//         │       ├── 输出 Compute Shader（打包缓冲 → displacementImage, normalAuxImage）
//         │       └── 图像屏障：COMPUTE_WRITE → GRAPHICS_READ
//         ├── BeginRenderPass
//         ├── 绑定图形管线（实体/线框）
//         ├── 绑定描述符集（currentFrame 的 UBO + 所有 Cascade 纹理）
//         ├── 绘制静态水面网格
//         └── EndRenderPass

// GPU 每帧数据流（完全在显存内）：
//   GPU 静态资源（h0, h0MinusConj, waveData）已存在
//   → 频谱演化 CS：根据时间 t 计算当前频域数据 → ping 缓冲区
//   → Stockham IFFT CS：频域 → 空间域（ping-pong）
//   → 输出 CS：打包缓冲 → displacementImage / normalAuxImage（GENERAL 布局）
//   → 屏障转换到 SHADER_READ_ONLY_OPTIMAL
//   → 顶点着色器采样位移纹理，偏移顶点
//   → 片段着色器输出最终颜色
// CPU 仅每帧传递时间参数和少量 push constants，无大数据传输。

void Stage7BoreFrontApp::Start()
{
    VKP_INFO("Stage7BoreFrontApp started");

    static_assert(sizeof(glm::vec4) == 16);

    m_Camera.AddYawPitch(0.0f, -300.0f);

    CreateDescriptorSetLayout();
    CreatePipelines();
    CreateWaterGrid();
    CreateSamplers();
    CreateBoreFrontResources();
    // descriptor sets 需要 WSTessendorfGPU 的 frame image info，所以先建 GPU source
    CreateGPUFFTSource();
    CreateUniformBuffers();
    CreateDescriptorPool();
    CreateDescriptorSets();
}

void Stage7BoreFrontApp::ShutdownApp()
{
    m_SolidPipeline.reset();
    m_WireframePipeline.reset();

    m_DescriptorSets.clear();
    m_FrontDerivativeTexture.reset();
    m_FrontParameterTexture.reset();
    m_DescriptorPool.reset();
    m_DescriptorSetLayout.reset();

    m_TessendorfGPU.reset();
    m_FFTSampler.reset();
    m_FrontLUTSampler.reset();

    m_WaterParamsUniformBuffers.clear();
    m_CameraUniformBuffers.clear();
    m_BoreFrontUniformBuffers.clear();

    m_WaterGrid.reset();
}

void Stage7BoreFrontApp::CreatePipelines()
{
    vkp::PipelineConfig config{};

    config.descriptorSetLayouts = {*m_DescriptorSetLayout};

    auto bindingDescription = water::WaterVertex::GetBindingDescription();
    auto attributeDescriptions = water::WaterVertex::GetAttributeDescriptions();

    config.bindingDescriptions = {bindingDescription};
    config.attributeDescriptions = {
        attributeDescriptions[0],
        attributeDescriptions[1],
        attributeDescriptions[2]
    };

    config.depthTestEnable = true;
    config.depthWriteEnable = true;
    config.depthCompareOp = VK_COMPARE_OP_LESS;

    config.cullMode = VK_CULL_MODE_NONE;
    config.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    config.polygonMode = VK_POLYGON_MODE_FILL;

    m_SolidPipeline = std::make_unique<vkp::Pipeline>(
        GetDevice(),
        GetRenderPass(),
        "shaders/water/stage7_bore_front.vert.spv",
        "shaders/water/stage7_bore_front.frag.spv",
        config
    );

    if(GetDevice().SupportsFillModeNonSolid()){
        vkp::PipelineConfig wireframeConfig = config;
        wireframeConfig.polygonMode = VK_POLYGON_MODE_LINE;

        m_WireframePipeline = std::make_unique<vkp::Pipeline>(
            GetDevice(),
            GetRenderPass(),
            "shaders/water/stage7_bore_front.vert.spv",
            "shaders/water/stage7_bore_front.frag.spv",
            wireframeConfig
        );
    }
}

// 它定义了一套接口规范：
    // 这个描述符集有多少个 binding。
    // 每个 binding 的类型是什么（UBO、纹理、存储缓冲等）。
    // 哪些着色器阶段可以访问（顶点、片段、计算）。
void Stage7BoreFrontApp::CreateDescriptorSetLayout()
{
    // 2 个 UBO + 6 个纹理
    // CameraUBO + WaterParamsUBO
    // 三层 FFT 分别负责短波（High）、中波（Mid）、长波（Low），
    // 每层都需要一张位移图和一张法线辅助图，所以总共 2×3 = 6 个纹理绑定
    m_DescriptorSetLayout = vkp::DescriptorSetLayout::Builder(GetDevice())
        .AddBinding(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT)
        .AddBinding(1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT)
        .AddBinding(2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT)
        .AddBinding(3, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT)
        .AddBinding(4, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT)
        .AddBinding(5, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT)
        .AddBinding(6, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT)
        .AddBinding(7, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT)
        .AddBinding(8, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT)
        .AddBinding(9, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT)
        .AddBinding(10, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT)
        .Build();
}

void Stage7BoreFrontApp::CreateWaterGrid()
{
    water::WaterGridConfig config{};
    config.cellCountX = 128;
    config.cellCountZ = 128;
    config.sizeX = 256.0f;
    config.sizeZ = 256.0f;
    config.origin = glm::vec3(0.0f);

    m_WaterGrid = std::make_unique<water::WaterGrid>(
        GetPhysicalDevice(),
        GetDevice(),
        GetCommandPool(),
        GetDevice().GetGraphicsQueue(),
        config,
        water::WaterGridUploadMode::StaticDeviceLocal
    );
}

void Stage7BoreFrontApp::CreateSamplers()
{
    VkFormatFeatureFlags linearFeatures =
        VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT |
        VK_FORMAT_FEATURE_TRANSFER_DST_BIT |
        VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT;

    VkFilter filter = VK_FILTER_NEAREST;

    if(GetDevice().SupportsFormatFeatures(
        VK_FORMAT_R32G32B32A32_SFLOAT,
        VK_IMAGE_TILING_OPTIMAL,
        linearFeatures
    )){
        filter = VK_FILTER_LINEAR;
    }

    m_FFTSampler = std::make_unique<water::WaterSampler>(
        GetDevice(),
        filter
    );

    m_FrontLUTSampler = std::make_unique<water::WaterSampler>(
        GetDevice(),
        VK_FILTER_LINEAR,
        VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE
    );
}

void Stage7BoreFrontApp::CreateBoreFrontResources()
{
    m_BoreFrontParams.origin = glm::vec2(0.0f);
    m_BoreFrontParams.direction = glm::normalize(glm::vec2(1.0f, 0.15f));
    m_BoreFrontParams.speed = 8.0f;
    m_BoreFrontParams.frontLength = 1000.0f;
    m_BoreFrontParams.initialOffset = -100.0f;
    m_BoreFrontParams.edgeFadeFraction = 0.03f;

    m_FrontLUT =
        water::GenerateDeterministicFrontLUT(1024);

    m_FrontParameterTexture =
        std::make_unique<water::StaticFloatTexture2D>(
            GetPhysicalDevice(),
            GetDevice(),
            GetCommandPool(),
            GetDevice().GetGraphicsQueue(),
            m_FrontLUT.resolution,
            1,
            m_FrontLUT.parameters
        );

    m_FrontDerivativeTexture =
        std::make_unique<water::StaticFloatTexture2D>(
            GetPhysicalDevice(),
            GetDevice(),
            GetCommandPool(),
            GetDevice().GetGraphicsQueue(),
            m_FrontLUT.resolution,
            1,
            m_FrontLUT.derivatives
        );
}

void Stage7BoreFrontApp::CreateGPUFFTSource()
{
    m_TessendorfGPU = std::make_unique<water::WSTessendorfGPU>(
        GetPhysicalDevice(),
        GetDevice(),
        GetCommandPool(),
        GetDevice().GetGraphicsQueue(),
        GetMaxFramesInFlight(),
        *m_FFTSampler,
        m_OceanConfig.spectrum,
        m_OceanConfig.amplitudeScales
    );
}

void Stage7BoreFrontApp::CreateUniformBuffers()
{
    m_BoreFrontUniformBuffers.clear();
    m_CameraUniformBuffers.clear();
    m_WaterParamsUniformBuffers.clear();

    m_CameraUniformBuffers.reserve(GetMaxFramesInFlight());
    m_WaterParamsUniformBuffers.reserve(GetMaxFramesInFlight());
    m_BoreFrontUniformBuffers.reserve(GetMaxFramesInFlight());

    for(uint32_t i = 0; i < GetMaxFramesInFlight(); i++){
        // 创建相机 UBO
        auto cameraBuffer = std::make_unique<vkp::Buffer>(
            GetPhysicalDevice(),
            GetDevice(),
            sizeof(CameraUBO),
            VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
        );
        cameraBuffer->Map();
        m_CameraUniformBuffers.push_back(std::move(cameraBuffer));

        // 创建水体参数 UBO
        auto waterParamsBuffer = std::make_unique<vkp::Buffer>(
            GetPhysicalDevice(),
            GetDevice(),
            sizeof(water::WaterParamsUBO),
            VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
        );
        waterParamsBuffer->Map();
        m_WaterParamsUniformBuffers.push_back(std::move(waterParamsBuffer));

        // 创建 BoreFront UBO
        auto boreFrontBuffer = std::make_unique<vkp::Buffer>(
            GetPhysicalDevice(),
            GetDevice(),
            sizeof(water::BoreFrontUBO),
            VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
        );
        boreFrontBuffer->Map();
        m_BoreFrontUniformBuffers.push_back(std::move(boreFrontBuffer));
    }
}

void Stage7BoreFrontApp::CreateDescriptorPool()
{
    m_DescriptorPool = vkp::DescriptorPool::Builder(GetDevice())
        .SetMaxSets(GetMaxFramesInFlight())
        .AddPoolSize(
            VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
            GetMaxFramesInFlight() * 3
        )
        .AddPoolSize(
            VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            GetMaxFramesInFlight() * 8
        )
        .Build();
}

// 为每一个飞行帧分配描述符集，并将具体的缓冲区和纹理绑定到着色器的槽位上。
// 负责创建图形着色器（顶点/片段着色器） 使用的描述符集
// 用途：为水面渲染绑定摄像机矩阵、水体参数、位移图、法线辅助图等资源。
// 绑定内容：CameraUBO、WaterParamsUBO、fftDisplacement0~2（三层的位移纹理）、fftNormalAux0~2（三层的法线辅助纹理）等。
// 着色器类型：顶点着色器（.vert）、片段着色器（.frag）。
// 描述符集布局：m_DescriptorSetLayout（在 Stage7BoreFrontApp 中创建，与计算管线的布局不同）。
void Stage7BoreFrontApp::CreateDescriptorSets()
{
    // 为每个飞行帧预留一个描述符集的空位
    m_DescriptorSets.resize(GetMaxFramesInFlight());

    // 遍历每一个飞行帧，逐个填充描述符集
    for(uint32_t i = 0; i < GetMaxFramesInFlight(); i++){
        
        // 1. 准备 camera UBO 的绑定信息
        VkDescriptorBufferInfo cameraInfo{};
        cameraInfo.buffer = *m_CameraUniformBuffers[i]; // 指向当前帧的相机 UBO
        cameraInfo.offset = 0;
        cameraInfo.range = sizeof(CameraUBO);           // 绑定整个 UBO

        // 2. 准备 水体参数 UBO 的绑定信息
        VkDescriptorBufferInfo waterParamsInfo{};
        waterParamsInfo.buffer = *m_WaterParamsUniformBuffers[i]; // 指向当前帧的水体参数 UBO
        waterParamsInfo.offset = 0;
        waterParamsInfo.range = sizeof(water::WaterParamsUBO);   // 绑定整个 UBO

        VkDescriptorBufferInfo boreFrontInfo{};
        boreFrontInfo.buffer = *m_BoreFrontUniformBuffers[i];
        boreFrontInfo.offset = 0;
        boreFrontInfo.range = sizeof(water::BoreFrontUBO);

        // 3. 准备 位移图 的绑定信息。
        //    GetDescriptorInfo 内部会封装 VkImageView 和 VkSampler，
        //    并指定图像布局为 SHADER_READ_ONLY_OPTIMAL，供着色器采样
        // 4. 准备 法线辅助图 的绑定信息，同样使用 FFTSampler
        water::WaterSurfaceGPUResources gpuResources =
            m_TessendorfGPU->GetGPUResources(i);

        VkDescriptorImageInfo frontParameterInfo =
            m_FrontParameterTexture->GetDescriptorInfo(*m_FrontLUTSampler);

        VkDescriptorImageInfo frontDerivativeInfo =
            m_FrontDerivativeTexture->GetDescriptorInfo(*m_FrontLUTSampler);

        // 5. 用 DescriptorWriter 将上述资源按顺序写入描述符集
        //    binding 0：camera UBO 每一帧，在 UpdateCameraUniformBuffer 中生成视图矩阵 投影矩阵 着色器通过 binding = 0 读取这个 UBO，完成顶点到裁剪空间的变换
        //    binding 1：water params UBO 存储控制波浪变形的全局参数(FFT 分辨率 补丁长度 choppy 强度 调试模式标志)
        //    binding 2：位移图（组合图像采样器）
        //    binding 3：法线辅助图（组合图像采样器）
        bool success = vkp::DescriptorWriter(*m_DescriptorSetLayout, *m_DescriptorPool)
            .WriteBuffer(0, &cameraInfo)
            .WriteBuffer(1, &waterParamsInfo)
            .WriteImage(2, &gpuResources.cascades[0].displacement)
            .WriteImage(3, &gpuResources.cascades[0].normalAux)
            .WriteImage(4, &gpuResources.cascades[1].displacement)
            .WriteImage(5, &gpuResources.cascades[1].normalAux)
            .WriteImage(6, &gpuResources.cascades[2].displacement)
            .WriteImage(7, &gpuResources.cascades[2].normalAux)
            .WriteBuffer(8, &boreFrontInfo)
            .WriteImage(9, &frontParameterInfo)
            .WriteImage(10, &frontDerivativeInfo)
            .Build(m_DescriptorSets[i]); // 完成分配与写入

        // 如果分配或写入失败（比如池子空间不足），立即抛出异常
        if(!success){
            throw std::runtime_error("Failed to allocate Stage6 GPU FFT descriptor set");
        }
    }
}

void Stage7BoreFrontApp::UpdateCamera(float deltaTime)
{
    float distance = 20.0f * deltaTime;

    if(m_Keys[GLFW_KEY_W]){
        m_Camera.MoveForward(distance);
    }

    if(m_Keys[GLFW_KEY_S]){
        m_Camera.MoveForward(-distance);
    }

    if(m_Keys[GLFW_KEY_A]){
        m_Camera.MoveRight(-distance);
    }

    if(m_Keys[GLFW_KEY_D]){
        m_Camera.MoveRight(distance);
    }

    if(m_Keys[GLFW_KEY_SPACE]){
        m_Camera.MoveUp(distance);
    }

    if(m_Keys[GLFW_KEY_LEFT_CONTROL]){
        m_Camera.MoveUp(-distance);
    }
}

void Stage7BoreFrontApp::Update(core::Timestep timestep)
{
    float deltaTime = timestep.GetSeconds();

    UpdateCamera(deltaTime);

    float simulationDeltaTime = 0.0f;

    if(!m_Paused){
        simulationDeltaTime = deltaTime;
    }

    if(m_StepOnce){
        simulationDeltaTime = 1.0f / 60.0f;
        m_StepOnce = false;
    }

    m_LastSimulationDeltaTime = simulationDeltaTime;
    m_Time += simulationDeltaTime;

    float boreDeltaTime = 0.0f;

    if(!m_BorePaused){
        boreDeltaTime = deltaTime;
    }

    m_LastBoreDeltaTime = boreDeltaTime;
    m_BoreTime += boreDeltaTime;

    UpdateWindowTitle();
}

void Stage7BoreFrontApp::PrepareFrame(uint32_t frameIndex, uint32_t imageIndex)
{
    UpdateCameraUniformBuffer(frameIndex);
    UpdateWaterParamsUniformBuffer(frameIndex);
    UpdateBoreFrontUniformBuffer(frameIndex);
}

void Stage7BoreFrontApp::UpdateCameraUniformBuffer(uint32_t frameIndex)
{
    VkExtent2D extent = GetSwapChain().GetExtent();

    float aspect =
        static_cast<float>(extent.width) /
        static_cast<float>(extent.height);

    CameraUBO ubo{};
    ubo.model = glm::mat4(1.0f);
    ubo.view = m_Camera.GetViewMatrix();
    ubo.projection = m_Camera.GetProjectionMatrix(aspect);
    ubo.debug = glm::ivec4(m_DebugMode, 0, 0, 0);

    m_CameraUniformBuffers[frameIndex]->CopyToMapped(
        &ubo,
        sizeof(ubo)
    );
}

void Stage7BoreFrontApp::UpdateWaterParamsUniformBuffer(uint32_t frameIndex)
{
    water::WaterParamsUBO ubo{}; // GPU 端的 UBO 结构体
    ubo.patchLengths = glm::vec4(
        m_OceanConfig.spectrum.shortPatchLength,
        m_OceanConfig.spectrum.midPatchLength,
        m_OceanConfig.spectrum.longPatchLength,
        0.0f
    );// 每个级联的补丁边长（米），决定波浪波长和频域采样间距

    ubo.amplitudeScales = glm::vec4(
        m_OceanConfig.amplitudeScales[0],
        m_OceanConfig.amplitudeScales[1],
        m_OceanConfig.amplitudeScales[2],
        0.0f
    ); // 每个级联的振幅缩放因子，影响波浪高度

    ubo.metadata = glm::ivec4(
        static_cast<int>(water::kMaxFFTCascades),
        0,
        0,
        0
    );// 元数据，包括最大级联数

    ubo.simulation = glm::vec4(
        m_Time,
        1.0f,
        1.0f,
        static_cast<float>(m_DebugMode)
    ); // 时间、模拟参数和调试模式

    m_WaterParamsUniformBuffers[frameIndex]->CopyToMapped(
        &ubo,
        sizeof(ubo)
    );
}

void Stage7BoreFrontApp::UpdateBoreFrontUniformBuffer(uint32_t frameIndex)
{
    glm::vec2 direction =
        glm::normalize(m_BoreFrontParams.direction);

    water::BoreFrontUBO ubo{};

    // ubo.originSpeedTime：
        // .x, .y = 波前原点 origin（世界坐标）某一特定时刻，波锋线（Front Line）在推进方向上所处的几何位置
        // .z = 推进速度 speed（米/秒）
        // .w = 当前累计时间 m_BoreTime（秒），即 speed * time 中的 time，由 CPU 每帧累积
    ubo.originSpeedTime = glm::vec4(
        m_BoreFrontParams.origin,
        m_BoreFrontParams.speed,
        m_BoreTime
    );

    // ubo.directionLengthFade：
        // .x, .y = 波前推进方向 direction（归一化单位向量）
        // .z = 波前总长度 frontLength（米）
        // .w = 两端淡出比例 edgeFadeFraction（例如 0.03 表示两端各 3% 长度用于平滑消失）
    ubo.directionLengthFade = glm::vec4(
        direction,
        m_BoreFrontParams.frontLength,
        m_BoreFrontParams.edgeFadeFraction
    );

    // ubo.motionDebug：
        // .x = 初始偏移量 initialOffset（米）
        // .y = 波前剖面宽度 profileWidth（此处固定为 5.0 米）
        // .z = 浪尖掩码强度 ridgeStrength（调试模式启用时为 10.0，否则为 0）
        // .w = 是否启用涌潮效果 boreEnabled（1.0 启用，0.0 关闭）
    ubo.motionDebug = glm::vec4(
        m_BoreFrontParams.initialOffset,
        5.0f,
        m_BoreDebugRidgeEnabled ? 10.0f : 0.0f,
        m_BoreEnabled ? 1.0f : 0.0f
    );

    // ubo.lutInfo：
        // .x = Front LUT 的采样分辨率（如 1024）
        // .y = 分辨率的倒数（用于着色器中将采样点索引映射到 0~1 纹理坐标）
        // .z = 是否使用 LUT（1.0 启用弯曲波前，0.0 使用直线波前）
        // .w = 预留（目前为 0.0）
    ubo.lutInfo = glm::vec4(
        static_cast<float>(m_FrontLUT.resolution),
        1.0f / static_cast<float>(m_FrontLUT.resolution),
        m_BoreUseLUT ? 1.0f : 0.0f,
        0.0f
    );

    m_BoreFrontUniformBuffers[frameIndex]->CopyToMapped(
        &ubo,
        sizeof(ubo)
    );
}

void Stage7BoreFrontApp::Render(VkCommandBuffer commandBuffer, uint32_t imageIndex)
{
    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;

    if(vkBeginCommandBuffer(commandBuffer, &beginInfo) != VK_SUCCESS){
        throw std::runtime_error("Failed to begin Stage6 command buffer");
    }

    uint32_t currentFrame = GetCurrentFrameIndex();

    VkRenderPassBeginInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    renderPassInfo.renderPass = GetRenderPass();
    renderPassInfo.framebuffer = GetSwapChain().GetFramebuffer(imageIndex);
    renderPassInfo.renderArea.offset = {0, 0};
    renderPassInfo.renderArea.extent = GetSwapChain().GetExtent();

    std::array<VkClearValue, 2> clearValues{};
    clearValues[0].color.float32[0] = 0.02f;
    clearValues[0].color.float32[1] = 0.03f;
    clearValues[0].color.float32[2] = 0.05f;
    clearValues[0].color.float32[3] = 1.0f;
    clearValues[1].depthStencil = {1.0f, 0};

    renderPassInfo.clearValueCount = static_cast<uint32_t>(clearValues.size());
    renderPassInfo.pClearValues = clearValues.data();

    // Compute Pass（计算 Pass） 触发 Compute Shader 完成 FFT 波浪模拟的全部 GPU 工作
    m_TessendorfGPU->UpdateGPU(
        commandBuffer,
        currentFrame,
        m_LastSimulationDeltaTime
    );
    
    // Graphics Pass（图形 Pass） 触发 Vertex Shader 和 Fragment Shader 完成网格绘制
    vkCmdBeginRenderPass(
        commandBuffer,
        &renderPassInfo,
        VK_SUBPASS_CONTENTS_INLINE
    );

    VkViewport viewport{};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = static_cast<float>(GetSwapChain().GetExtent().width);
    viewport.height = static_cast<float>(GetSwapChain().GetExtent().height);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;

    vkCmdSetViewport(commandBuffer, 0, 1, &viewport);

    VkRect2D scissor{};
    scissor.offset = {0, 0};
    scissor.extent = GetSwapChain().GetExtent();

    vkCmdSetScissor(commandBuffer, 0, 1, &scissor);

    vkp::Pipeline* pipeline = m_SolidPipeline.get();

    if(m_UseWireframe && m_WireframePipeline){
        pipeline = m_WireframePipeline.get();
    }

    vkCmdBindPipeline(
        commandBuffer,
        VK_PIPELINE_BIND_POINT_GRAPHICS,
        *pipeline
    );

    vkCmdBindDescriptorSets(
        commandBuffer,
        VK_PIPELINE_BIND_POINT_GRAPHICS,
        pipeline->GetLayout(),
        0,
        1,
        &m_DescriptorSets[currentFrame],
        0,
        nullptr
    );

    m_WaterGrid->Bind(commandBuffer);
    m_WaterGrid->Draw(commandBuffer);

    vkCmdEndRenderPass(commandBuffer);

    if(vkEndCommandBuffer(commandBuffer) != VK_SUCCESS){
        throw std::runtime_error("Failed to record Stage6 command buffer");
    }
}
void Stage7BoreFrontApp::UpdateWindowTitle()
{
    m_TitleUpdateTimer += 1.0f / 60.0f;

    if(m_TitleUpdateTimer < 0.5f){
        return;
    }

    m_TitleUpdateTimer = 0.0f;

    const glm::vec3& pos = m_Camera.GetPosition();

    std::ostringstream title;
    title << "Stage 7 - Bore Front Field | pos=("
        << pos.x << ", "
        << pos.y << ", "
        << pos.z << ") mode="
        << m_DebugMode
        << " wire="
        << (m_UseWireframe ? "on" : "off")
        << " paused="
        << (m_Paused ? "yes" : "no");

    GetWindow().SetTitle(title.str());
}

void Stage7BoreFrontApp::OnFramebufferResize(int width, int height)
{
    core::Application::OnFramebufferResize(width, height);
}

void Stage7BoreFrontApp::OnKey(int key, int scancode, int action, int mods)
{
    // 保留基类的默认按键行为（如 ESC 退出等）
    core::Application::OnKey(key, scancode, action, mods);

    // 忽略非法键值，防止数组越界
    if(key < 0 || key >= 1024){
        return;
    }

    if(action == GLFW_PRESS){
        m_Keys[key] = true;

        // F1：基础水面光照（片段着色器 mode = 0）
        // 显示带简单漫反射光照的水面颜色，用于评估波浪几何体在光照下的真实感
        if(key == GLFW_KEY_F1){
            m_DebugMode = 0;
        }

        // F2：高度场灰度图（片段着色器 mode = 1）
        // 将 FFT 波浪高度映射为灰度，亮处为波峰、暗处为波谷，检查波浪垂直位移分布
        if(key == GLFW_KEY_F2){
            m_DebugMode = 1;
        }

        // F3：水平位移可视化（片段着色器 mode = 2）
        // 用红蓝通道分别显示 X/Z 方向的水平位移（choppy），中间灰表示无偏移
        if(key == GLFW_KEY_F3){
            m_DebugMode = 2;
        }

        // F4：法线扰动斜率可视化（片段着色器 mode = 3）
        // 将法线辅助纹理中的 slopeX/slopeZ 映射到红/蓝通道，检验法线扰动是否正确
        if(key == GLFW_KEY_F4){
            m_DebugMode = 3;
        }

        // F5：波浪破碎/泡沫判据可视化（片段着色器 mode = 4）
        // 根据 Jacobian 行列式显示泡沫候选区域，越白表示越可能产生白浪
        if(key == GLFW_KEY_F5){
            m_DebugMode = 4;
        }

        // F6：世界空间法线可视化（片段着色器 mode = 5）
        // RGB 编码世界空间法线方向，绿 = 朝上，红/蓝 = 倾斜，纯色表示法线朝向异常
        if(key == GLFW_KEY_F6){
            m_DebugMode = 5;
        }

        // F7：波前带符号距离可视化（片段着色器 mode = 6）
        // 蓝红分区 + 白色波前峰线，检查涌潮推进方向、波前位置是否正确
        if(key == GLFW_KEY_F7){
            m_DebugMode = 6;
        }

        // F8：波前长度淡入淡出掩码（片段着色器 mode = 7）
        // 检查 1 km 波前两端的平滑消失效果，边缘应为渐变而非硬切
        if(key == GLFW_KEY_F8){
            m_DebugMode = 7;
        }

        // F9：标准化波前线坐标（片段着色器 mode = 8）
        // 检查 frontU 从 0 到 1 的映射是否正确，绿红渐变应沿波前切向变化
        if(key == GLFW_KEY_F9){
            m_DebugMode = 8;
        }

        // F10：局部波前法线方向（片段着色器 mode = 9）
        // 直线波前时法线应均匀一致，弯曲波前时法线应随 LUT 偏移旋转
        if(key == GLFW_KEY_F10){
            m_DebugMode = 9;
        }

        // F11：振幅乘数（片段着色器 mode = 10）
        // 检查 Front LUT 的 G 通道是否正确控制波高空间分布
        if(key == GLFW_KEY_F11){
            m_DebugMode = 10;
        }

        // F12：泡沫乘数（片段着色器 mode = 11）
        // 检查 Front LUT 的 B 通道是否正确控制泡沫强度空间分布
        if(key == GLFW_KEY_F12){
            m_DebugMode = 11;
        }

        // M：轮廓相位偏移可视化（profilePhaseOffset，Front LUT 的 A 通道）
        // 灰阶表示 Wave Profile 动画沿波前线的相位偏移量
        // 0 = 黑色（无偏移），1 = 白色（最大偏移）
        if(key == GLFW_KEY_M){
            m_DebugMode = (m_DebugMode + 1) % 13;
        }

        // Tab：切换线框渲染模式
        // 在实体填充和三角形线框之间切换，用于观察水面网格密度和变形情况
        if(key == GLFW_KEY_TAB){
            m_UseWireframe = !m_UseWireframe;
        }

        // P：暂停/继续水面模拟
        // 暂停时波浪停止演化（时间不再推进），便于观察某一时刻的波形细节
        if(key == GLFW_KEY_P){
            m_Paused = !m_Paused;
        }

        // O：单步执行一帧
        // 按下后模拟前进一帧（固定 1/60 秒）然后暂停，用于逐帧调试波浪动画
        if(key == GLFW_KEY_O){
            m_StepOnce = true;
        }

        // R：重置涌潮波前时间
        // 将 BoreTime 归零，使一线潮回到初始位置（origin + initialOffset）
        if(key == GLFW_KEY_R){
            m_BoreTime = 0.0f;
        }

        // C：切换 Front LUT 的使用
        // true = 使用弯曲波前（从 LUT 采样偏移量和法线修正），false = 直线波前
        if(key == GLFW_KEY_C){
            m_BoreUseLUT = !m_BoreUseLUT;
        }

        // B：整体开关涌潮效果
        // true = 叠加一线潮位移到水面网格，false = 仅显示背景 FFT 波浪
        if(key == GLFW_KEY_B){
            m_BoreEnabled = !m_BoreEnabled;
        }

        // T：切换涌潮浪尖掩码调试
        // 控制 ridgeStrength 参数，启用时浪尖区域会以 0.35 的强度增强泡沫/位移
        if(key == GLFW_KEY_T){
            m_BoreDebugRidgeEnabled = !m_BoreDebugRidgeEnabled;
        }

        // I：暂停/继续涌潮时间
        // 暂停时 BoreTime 不再累加，波前停在当前位置，但背景波浪仍可正常演化
        if(key == GLFW_KEY_I){
            m_BorePaused = !m_BorePaused;
        }
    }
    else if(action == GLFW_RELEASE){
        m_Keys[key] = false;
    }   
}

void Stage7BoreFrontApp::OnMouseMove(double x, double y)
{
    if(!m_CameraControlEnabled){
        return;
    }

    if(m_FirstMouse){
        m_LastMouseX = x;
        m_LastMouseY = y;
        m_FirstMouse = false;
        return;
    }

    double dx = x - m_LastMouseX;
    double dy = y - m_LastMouseY;

    m_LastMouseX = x;
    m_LastMouseY = y;

    m_Camera.AddYawPitch(
        static_cast<float>(dx),
        static_cast<float>(-dy)
    );
}

void Stage7BoreFrontApp::OnMouseButton(int button, int action, int mods)
{
    if(button == GLFW_MOUSE_BUTTON_RIGHT && action == GLFW_PRESS){
        m_CameraControlEnabled = !m_CameraControlEnabled;
        m_FirstMouse = true;
    }
}