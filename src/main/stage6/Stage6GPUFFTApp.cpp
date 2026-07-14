#include "main/stage6/Stage6GPUFFTApp.h"

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
// ├── VKP_INFO("Stage6GPUFFTApp started")
// ├── static_assert(sizeof(glm::vec4) == 16)
// ├── m_Camera.AddYawPitch(0.0f, -300.0f)         // 初始俯仰角
// ├── CreateDescriptorSetLayout()                 // 定义 set=0 的绑定布局
// ├── CreatePipelines()                           // 创建实体和线框管线
// │   ├── 配置 PipelineConfig（descriptorSetLayout、顶点输入、深度、面剔除等）
// │   ├── m_SolidPipeline = new vkp::Pipeline(...)   (polygonMode = FILL)
// │   └── if (支持 VK_POLYGON_MODE_LINE) → m_WireframePipeline = ...
// ├── CreateWaterGrid()                           // 创建静态水面网格
// │   ├── 配置 WaterGridConfig（128×128，256×256m）
// │   └── m_WaterGrid = new WaterGrid(..., StaticDeviceLocal)   // 顶点/索引缓冲上传到 DEVICE_LOCAL
// ├── CreateTessendorfSource()                    // 创建 Tessendorf FFT 模拟器
// │   ├── 初始化 TessendorfSpectrumParams（分辨率、风速、patch 等）
// │   └── m_Tessendorf = new WSTessendorfCPU(params)
// ├── CreateSamplers()                            // 创建采样器
// │   ├── 检查设备是否支持线性过滤 → 选择 NEAREST 或 LINEAR
// │   └── m_FFTSampler = new WaterSampler(device, filter)
// ├── CreateUniformBuffers()                      // 创建每帧 UBO
// │   ├── m_CameraUniformBuffers（每帧一个，HOST_VISIBLE + COHERENT，Map 持久映射）
// │   └── m_WaterParamsUniformBuffers（同上）
// ├── CreateCPUFFTFrameResources()                // 创建每帧动态纹理资源
// │   ├── 检查 R32G32B32A32_SFLOAT 格式支持
// │   └── for (each frame in flight)
// │       ├── displacementStaging (HOST_VISIBLE + TRANSFER_SRC, Map)
// │       ├── normalAuxStaging (同上)
// │       ├── displacementImage = new DynamicImage2D(...)   (DEVICE_LOCAL, TRANSFER_DST | SAMPLED)
// │       └── normalAuxImage = new DynamicImage2D(...)
// ├── CreateDescriptorPool()                      // 分配描述符池（UBO × 2N, COMBINED_IMAGE_SAMPLER × 2N）
// └── CreateDescriptorSets()                      // 为每帧分配并更新描述符集
//     └── for (each frame)
//         ├── 相机 UBO → binding 0
//         ├── 水体参数 UBO → binding 1
//         ├── displacementImage + sampler → binding 2
//         └── normalAuxImage + sampler → binding 3

// Loop()
// └── for each frame
//     ├── Update(timestep)                    // 更新逻辑、相机、FFT 计算
//     ├── PrepareFrame(frameIndex, imageIndex) // 拷贝 FFT 结果到 staging，更新 UBO
//     └── Render(commandBuffer, imageIndex)   // 录制命令，上传纹理，绘制水面

// CPU 每帧：
//   m_Tessendorf->ComputeAtTime(m_Time)
//     → 频谱演化 + IFFT
//     → CPUWaterSurfaceFrame (位移, 法线辅助)
// PrepareFrame:
//   → 拷贝至 staging buffers (HOST_VISIBLE)
//   → 更新相机 UBO, 水体参数 UBO
// Render:
//   → RecordUpload: staging buffer → DynamicImage2D (DEVICE_LOCAL) 纹理
//   → 渲染通道: 绑定管线 + 描述符集 (UBO, 纹理)
//   → 顶点着色器采样位移纹理，偏移顶点
//   → 片段着色器输出调试颜色 / 法线等

void Stage6GPUFFTApp::Start()
{
    VKP_INFO("Stage6GPUFFTApp started");

    static_assert(sizeof(glm::vec4) == 16);

    m_Camera.AddYawPitch(0.0f, -300.0f);

    CreateDescriptorSetLayout();
    CreatePipelines();
    CreateWaterGrid();
    CreateSamplers();
    // descriptor sets 需要 WSTessendorfGPU 的 frame image info，所以先建 GPU source
    CreateGPUFFTSource();
    CreateUniformBuffers();
    CreateDescriptorPool();
    CreateDescriptorSets();
}

void Stage6GPUFFTApp::ShutdownApp()
{
    m_SolidPipeline.reset();
    m_WireframePipeline.reset();

    m_DescriptorSets.clear();
    m_DescriptorPool.reset();
    m_DescriptorSetLayout.reset();

    m_TessendorfGPU.reset();
    m_FFTSampler.reset();

    m_WaterParamsUniformBuffers.clear();
    m_CameraUniformBuffers.clear();

    m_WaterGrid.reset();
}

void Stage6GPUFFTApp::CreatePipelines()
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
        "shaders/water/stage6_water.vert.spv",
        "shaders/water/stage6_water.frag.spv",
        config
    );

    if(GetDevice().SupportsFillModeNonSolid()){
        vkp::PipelineConfig wireframeConfig = config;
        wireframeConfig.polygonMode = VK_POLYGON_MODE_LINE;

        m_WireframePipeline = std::make_unique<vkp::Pipeline>(
            GetDevice(),
            GetRenderPass(),
            "shaders/water/stage6_water.vert.spv",
            "shaders/water/stage6_water.frag.spv",
            wireframeConfig
        );
    }
}

void Stage6GPUFFTApp::CreateDescriptorSetLayout()
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
        .Build();
}

void Stage6GPUFFTApp::CreateWaterGrid()
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

void Stage6GPUFFTApp::CreateSamplers()
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
}

void Stage6GPUFFTApp::CreateGPUFFTSource()
{
    water::TessendorfSpectrumParams params{};
    params.resolution = m_FFTResolution;
    params.patchLength = m_PatchLength;
    params.windDirection = glm::normalize(glm::vec2(1.0f, 0.25f));
    params.windSpeed = 25.0f;
    params.spectrumAmplitude = 2.5f;
    params.shortWaveDamping = 0.001f;
    params.gravity = 9.81f;
    params.choppyLambda = 1.0f;
    params.oppositeWindDamping = 0.07f;
    params.randomSeed = 1337;

    m_TessendorfGPU = std::make_unique<water::WSTessendorfGPU>(
        GetPhysicalDevice(),
        GetDevice(),
        GetCommandPool(),
        GetDevice().GetGraphicsQueue(),
        GetMaxFramesInFlight(),
        *m_FFTSampler,
        params
    );
}

void Stage6GPUFFTApp::CreateUniformBuffers()
{
    m_CameraUniformBuffers.clear();
    m_WaterParamsUniformBuffers.clear();

    m_CameraUniformBuffers.reserve(GetMaxFramesInFlight());
    m_WaterParamsUniformBuffers.reserve(GetMaxFramesInFlight());

    for(uint32_t i = 0; i < GetMaxFramesInFlight(); i++){
        auto cameraBuffer = std::make_unique<vkp::Buffer>(
            GetPhysicalDevice(),
            GetDevice(),
            sizeof(CameraUBO),
            VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
        );

        cameraBuffer->Map();

        m_CameraUniformBuffers.push_back(std::move(cameraBuffer));

        auto waterParamsBuffer = std::make_unique<vkp::Buffer>(
            GetPhysicalDevice(),
            GetDevice(),
            sizeof(water::WaterParamsUBO),
            VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
        );

        waterParamsBuffer->Map();

        m_WaterParamsUniformBuffers.push_back(std::move(waterParamsBuffer));
    }
}

void Stage6GPUFFTApp::CreateDescriptorPool()
{
    m_DescriptorPool = vkp::DescriptorPool::Builder(GetDevice())
        .SetMaxSets(GetMaxFramesInFlight())
        .AddPoolSize(
            VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
            GetMaxFramesInFlight() * 6
        )
        .AddPoolSize(
            VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            GetMaxFramesInFlight() * 6
        )
        .Build();
}

// 为每一个飞行帧分配描述符集，并将具体的缓冲区和纹理绑定到着色器的槽位上。
void Stage6GPUFFTApp::CreateDescriptorSets()
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

        // 3. 准备 位移图 的绑定信息。
        //    GetDescriptorInfo 内部会封装 VkImageView 和 VkSampler，
        //    并指定图像布局为 SHADER_READ_ONLY_OPTIMAL，供着色器采样。
        // 4. 准备 法线辅助图 的绑定信息，同样使用 FFTSampler
        VkDescriptorImageInfo displacementInfo =
            m_TessendorfGPU->GetFrameDisplacementInfo(i);

        VkDescriptorImageInfo normalAuxInfo =
            m_TessendorfGPU->GetFrameNormalAuxInfo(i);

        // 5. 用 DescriptorWriter 将上述资源按顺序写入描述符集
        //    binding 0：camera UBO 每一帧，在 UpdateCameraUniformBuffer 中生成视图矩阵 投影矩阵 着色器通过 binding = 0 读取这个 UBO，完成顶点到裁剪空间的变换
        //    binding 1：water params UBO 存储控制波浪变形的全局参数(FFT 分辨率 补丁长度 choppy 强度 调试模式标志)
        //    binding 2：位移图（组合图像采样器）
        //    binding 3：法线辅助图（组合图像采样器）
        bool success = vkp::DescriptorWriter(*m_DescriptorSetLayout, *m_DescriptorPool)
            .WriteBuffer(0, &cameraInfo)
            .WriteBuffer(1, &waterParamsInfo)
            .WriteImage(2, &displacementInfo)
            .WriteImage(3, &normalAuxInfo)
            .WriteImage(4, &displacementInfo)
            .WriteImage(5, &normalAuxInfo)
            .WriteImage(6, &displacementInfo)
            .WriteImage(7, &normalAuxInfo)
            .Build(m_DescriptorSets[i]); // 完成分配与写入

        // 如果分配或写入失败（比如池子空间不足），立即抛出异常
        if(!success){
            throw std::runtime_error("Failed to allocate Stage6 CPU FFT descriptor set");
        }
    }
}

void Stage6GPUFFTApp::UpdateCamera(float deltaTime)
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

void Stage6GPUFFTApp::Update(core::Timestep timestep)
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

    UpdateWindowTitle();
}

void Stage6GPUFFTApp::PrepareFrame(uint32_t frameIndex, uint32_t imageIndex)
{
    UpdateCameraUniformBuffer(frameIndex);
    UpdateWaterParamsUniformBuffer(frameIndex);
}

void Stage6GPUFFTApp::UpdateCameraUniformBuffer(uint32_t frameIndex)
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

void Stage6GPUFFTApp::UpdateWaterParamsUniformBuffer(uint32_t frameIndex)
{
    water::WaterParamsUBO ubo{}; // CPU 端的 UBO 结构体
    ubo.patchLengths = glm::vec4(
        m_PatchLength,
        m_PatchLength,
        m_PatchLength,
        0.0f
    ); // 每个级联的补丁边长（米），决定波浪波长和频域采样间距

    ubo.amplitudeScales = glm::vec4(
        1.0f,
        0.0f,
        0.0f,
        0.0f
    ); // 每个级联的振幅缩放因子，影响波浪高度

    ubo.metadata = glm::ivec4(
        1,
        0,
        0,
        0
    ); // 元数据，包括最大级联数

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

void Stage6GPUFFTApp::Render(VkCommandBuffer commandBuffer, uint32_t imageIndex)
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

    m_TessendorfGPU->SetFrameIndex(currentFrame);

    m_TessendorfGPU->UpdateGPU(
        commandBuffer,
        m_LastSimulationDeltaTime
    );
    
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
void Stage6GPUFFTApp::UpdateWindowTitle()
{
    m_TitleUpdateTimer += 1.0f / 60.0f;

    if(m_TitleUpdateTimer < 0.5f){
        return;
    }

    m_TitleUpdateTimer = 0.0f;

    const glm::vec3& pos = m_Camera.GetPosition();

    std::ostringstream title;
    title << "Stage 6 - GPU FFT | pos=("
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

void Stage6GPUFFTApp::OnFramebufferResize(int width, int height)
{
    core::Application::OnFramebufferResize(width, height);
}

void Stage6GPUFFTApp::OnKey(int key, int scancode, int action, int mods)
{
    core::Application::OnKey(key, scancode, action, mods);

    if(key < 0 || key >= 1024){
        return;
    }

    if(action == GLFW_PRESS){
        m_Keys[key] = true;

        if(key == GLFW_KEY_F1){
            m_DebugMode = 0;
        }

        if(key == GLFW_KEY_F2){
            m_DebugMode = 1;
        }

        if(key == GLFW_KEY_F3){
            m_DebugMode = 2;
        }

        if(key == GLFW_KEY_F4){
            m_DebugMode = 3;
        }

        if(key == GLFW_KEY_F5){
            m_DebugMode = 4;
        }

        if(key == GLFW_KEY_F6){
            m_DebugMode = 5;
        }

        if(key == GLFW_KEY_TAB){
            m_UseWireframe = !m_UseWireframe;
        }

        if(key == GLFW_KEY_P){
            m_Paused = !m_Paused;
        }

        if(key == GLFW_KEY_O){
            m_StepOnce = true;
        }
    }
    else if(action == GLFW_RELEASE){
        m_Keys[key] = false;
    }
}

void Stage6GPUFFTApp::OnMouseMove(double x, double y)
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

void Stage6GPUFFTApp::OnMouseButton(int button, int action, int mods)
{
    if(button == GLFW_MOUSE_BUTTON_RIGHT && action == GLFW_PRESS){
        m_CameraControlEnabled = !m_CameraControlEnabled;
        m_FirstMouse = true;
    }
}