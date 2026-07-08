#include "main/stage5/Stage5WaterGridApp.h"

#include "core/Log.h"

#include "scene/water/render/WaterVertex.h"

#include <GLFW/glfw3.h>
#include <glm/gtc/constants.hpp>

#include <array>
#include <cmath>
#include <sstream>
#include <stdexcept>

namespace
{
glm::vec2 RotateDirection(glm::vec2 direction, float degrees)
{   // 将方向向量绕原点旋转指定角度
    float radians = glm::radians(degrees); // 将角度转换为弧度

    float c = std::cos(radians);
    float s = std::sin(radians);

    glm::vec2 rotated{
        direction.x * c - direction.y * s,
        direction.x * s + direction.y * c
    };

    return glm::normalize(rotated);
}
}

void Stage5WaterGridApp::Start()
{
    VKP_INFO("Stage5WaterGridApp started");

    m_Camera.AddYawPitch(0.0f, -300.0f);

    CreateDescriptorSetLayout();    // 创建描述符集布局
    CreatePipelines();              // 创建管线
    CreateWaterGrid();              // 创建水网格
    CreateGerstnerSource();         // 创建 Gerstner 源 
    CreateUniformBuffers();         // 创建统一缓冲区
    CreateDescriptorPool();         // 创建描述符池
    CreateDescriptorSets();         // 创建描述符集
}

void Stage5WaterGridApp::ShutdownApp()
{
    m_SolidPipeline.reset();
    m_WireframePipeline.reset();

    m_DescriptorSets.clear();
    m_DescriptorPool.reset();
    m_DescriptorSetLayout.reset();

    m_CameraUniformBuffers.clear();

    m_WaterGrid.reset();
}

void Stage5WaterGridApp::CreateDescriptorSetLayout()
{
    m_DescriptorSetLayout = vkp::DescriptorSetLayout::Builder(GetDevice())
        .AddBinding(
            0,
            VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
            VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT
        )
        .Build();
}

void Stage5WaterGridApp::CreatePipelines()
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
        "shaders/water/water_grid.vert.spv",
        "shaders/water/water_grid.frag.spv",
        config
    );

    if(GetDevice().SupportsFillModeNonSolid()){
        vkp::PipelineConfig wireframeConfig = config;
        wireframeConfig.polygonMode = VK_POLYGON_MODE_LINE;

        m_WireframePipeline = std::make_unique<vkp::Pipeline>(
            GetDevice(),
            GetRenderPass(),
            "shaders/water/water_grid.vert.spv",
            "shaders/water/water_grid.frag.spv",
            wireframeConfig
        );
    }
    else{
        VKP_INFO("fillModeNonSolid unsupported, wireframe disabled");
    }
}

void Stage5WaterGridApp::CreateWaterGrid()
{   // 创建水网格
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
        config
    );

    VKP_INFO("Water grid vertices: {}", m_WaterGrid->GetBaseVertices().size());
    VKP_INFO("Water grid indices: {}", 128 * 128 * 6);
}

void Stage5WaterGridApp::CreateGerstnerSource()
{   // 创建 Gerstner 源
    // 固定 6 条测试波，验证叠加、相位、水平位移、法线
    glm::vec2 windDirection = glm::normalize(glm::vec2(1.0f, 0.25f));

    std::vector<water::GerstnerWave> waves;

    waves.push_back({
        windDirection,
        1.2f,
        48.0f,
        5.5f,
        0.40f,
        0.0f
    });

    waves.push_back({
        RotateDirection(windDirection, 12.0f),
        0.7f,
        30.0f,
        5.0f,
        0.35f,
        1.7f
    });

    waves.push_back({
        RotateDirection(windDirection, -18.0f),
        0.35f,
        18.0f,
        4.5f,
        0.30f,
        2.3f
    });

    waves.push_back({
        RotateDirection(windDirection, 35.0f),
        0.15f,
        10.0f,
        3.8f,
        0.25f,
        0.9f
    });

    waves.push_back({
        RotateDirection(windDirection, -40.0f),
        0.07f,
        6.0f,
        3.2f,
        0.20f,
        2.9f
    });

    waves.push_back({
        RotateDirection(windDirection, 90.0f),
        0.025f,
        3.0f,
        2.5f,
        0.15f,
        1.3f
    });

    m_GerstnerSource = std::make_shared<water::WSGerstnerCPU>(waves);
    m_Composer.AddSource(m_GerstnerSource);
}

void Stage5WaterGridApp::CreateUniformBuffers()
{   // 创建统一缓冲区
    VkDeviceSize bufferSize = sizeof(CameraUBO);

    m_CameraUniformBuffers.clear();
    m_CameraUniformBuffers.reserve(GetMaxFramesInFlight());

    for(uint32_t i = 0; i < GetMaxFramesInFlight(); i++){
        auto uniformBuffer = std::make_unique<vkp::Buffer>(
            GetPhysicalDevice(),
            GetDevice(),
            bufferSize,
            VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
        );

        uniformBuffer->Map();

        m_CameraUniformBuffers.push_back(std::move(uniformBuffer));
    }
}

void Stage5WaterGridApp::CreateDescriptorPool()
{   // 创建描述符池
    m_DescriptorPool = vkp::DescriptorPool::Builder(GetDevice())
        .SetMaxSets(GetMaxFramesInFlight())
        .AddPoolSize(
            VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
            GetMaxFramesInFlight()
        )
        .Build();
}

void Stage5WaterGridApp::CreateDescriptorSets()
{   // 创建描述符集
    m_DescriptorSets.resize(GetMaxFramesInFlight());

    for(uint32_t i = 0; i < GetMaxFramesInFlight(); i++){
        VkDescriptorBufferInfo bufferInfo{};
        bufferInfo.buffer = *m_CameraUniformBuffers[i];
        bufferInfo.offset = 0;
        bufferInfo.range = sizeof(CameraUBO);

        bool success = vkp::DescriptorWriter(*m_DescriptorSetLayout, *m_DescriptorPool)
            .WriteBuffer(0, &bufferInfo)
            .Build(m_DescriptorSets[i]);

        if(!success){
            throw std::runtime_error("Failed to allocate water grid descriptor set!");
        }
    }
}

void Stage5WaterGridApp::UpdateCameraUniformBuffer()
{   // 更新相机统一缓冲区
    VkExtent2D extent = GetSwapChain().GetExtent();

    float aspect = static_cast<float>(extent.width) /
        static_cast<float>(extent.height);

    CameraUBO ubo{};
    ubo.model = glm::mat4(1.0f);
    ubo.view = m_Camera.GetViewMatrix();
    ubo.projection = m_Camera.GetProjectionMatrix(aspect);
    ubo.debug = glm::ivec4(m_DebugMode, 0, 0, 0);

    m_CameraUniformBuffers[GetCurrentFrameIndex()]->CopyToMapped(
        &ubo,
        sizeof(ubo)
    );
}

void Stage5WaterGridApp::UpdateWaterSimulation(float deltaTime)
{   // 更新水模拟
    m_Composer.Update(deltaTime); // ICPUWaterSurfaceSource 更新水面高度

    const std::vector<water::WaterVertex>& baseVertices =
        m_WaterGrid->GetBaseVertices();

    m_DeformedVertices = baseVertices;

    for(size_t i = 0; i < m_DeformedVertices.size(); i++){
        const water::WaterVertex& base = baseVertices[i];
        water::WaterVertex& vertex = m_DeformedVertices[i];

        glm::vec2 worldXZ{
            base.position.x,
            base.position.z
        };

        water::WaterSurfaceSample sample =
            m_Composer.Sample(worldXZ);

        vertex.position =
            base.position +
            glm::vec3(
                sample.horizontalDisplacement.x,
                sample.height,
                sample.horizontalDisplacement.y
            );

        vertex.normal = glm::normalize(
            glm::vec3(
                -sample.slope.x,
                 1.0f,
                -sample.slope.y
            )
        );
    }

    m_WaterGrid->UpdateVertices(m_DeformedVertices);
}

void Stage5WaterGridApp::Update(core::Timestep timestep)
{
    float dt = timestep.GetSeconds();

    m_Time += dt;

    float distance = 20.0f * dt;

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

    float simulationDt = 0.0f;

    if(!m_Paused){
        simulationDt = dt;
    }

    if(m_StepOnce){
        simulationDt = 1.0f / 60.0f;
        m_StepOnce = false;
    }

    UpdateWaterSimulation(simulationDt);
    UpdateCameraUniformBuffer();
    UpdateWindowTitle();
}

void Stage5WaterGridApp::UpdateWindowTitle()
{
    m_TitleUpdateTimer += 1.0f / 60.0f;

    if(m_TitleUpdateTimer < 0.5f){
        return;
    }

    m_TitleUpdateTimer = 0.0f;

    const glm::vec3& pos = m_Camera.GetPosition();

    std::ostringstream title;
    title << "Stage 5 - Gerstner Grid | pos=("
        << pos.x << ", "
        << pos.y << ", "
        << pos.z << ") yaw="
        << m_Camera.GetYaw()
        << " pitch="
        << m_Camera.GetPitch()
        << " mode="
        << m_DebugMode
        << " wire="
        << (m_UseWireframe ? "on" : "off")
        << " paused="
        << (m_Paused ? "yes" : "no");

    GetWindow().SetTitle(title.str());
}

void Stage5WaterGridApp::Render(VkCommandBuffer commandBuffer, uint32_t imageIndex)
{
    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;

    if(vkBeginCommandBuffer(commandBuffer, &beginInfo) != VK_SUCCESS){
        throw std::runtime_error("Failed to begin recording command buffer!");
    }

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

    uint32_t currentFrame = GetCurrentFrameIndex();

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
        throw std::runtime_error("Failed to record command buffer!");
    }
}

void Stage5WaterGridApp::OnFramebufferResize(int width, int height)
{
    core::Application::OnFramebufferResize(width, height);
    VKP_INFO("Framebuffer resized: {} x {}", width, height);
}

void Stage5WaterGridApp::OnKey(int key, int scancode, int action, int mods)
{
    core::Application::OnKey(key, scancode, action, mods);

    if(key < 0 || key >= 1024){
        return;
    }

    if(action == GLFW_PRESS){
        m_Keys[key] = true;

        if(key == GLFW_KEY_F1){
            m_UseWireframe = false;
            m_DebugMode = 0;
        }

        if(key == GLFW_KEY_F2){
            m_UseWireframe = true;
        }

        if(key == GLFW_KEY_F3){
            m_DebugMode = 1;
        }

        if(key == GLFW_KEY_F4){
            m_DebugMode = 3;
        }

        if(key == GLFW_KEY_P){
            m_Paused = !m_Paused;
        }

        if(key == GLFW_KEY_O){
            m_StepOnce = true;
            m_Paused = true;
        }

        if(key == GLFW_KEY_R){
            if(m_GerstnerSource){
                m_GerstnerSource->ResetTime();
            }
        }
    }
    else if(action == GLFW_RELEASE){
        m_Keys[key] = false;
    }
}

void Stage5WaterGridApp::OnMouseMove(double x, double y)
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

    double xoffset = x - m_LastMouseX;
    double yoffset = m_LastMouseY - y;

    m_LastMouseX = x;
    m_LastMouseY = y;

    m_Camera.AddYawPitch(
        static_cast<float>(xoffset),
        static_cast<float>(yoffset)
    );
}

void Stage5WaterGridApp::OnMouseButton(int button, int action, int mods)
{
    core::Application::OnMouseButton(button, action, mods);
}