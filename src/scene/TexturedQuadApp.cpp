#include "scene/TexturedQuadApp.h"
#include "scene/Vertex.h"

#include "core/Log.h"

#include "vulkan/Pipeline.h"

#include <GLFW/glfw3.h>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <cmath>
#include <stdexcept>
#include <sstream> // 用于格式化字符串
#include <vector>
#include <cstdint>

static const std::vector<Vertex> s_Vertices = {
    {{-0.5f, -0.5f, 0.0f}, {1.0f, 0.0f, 0.0f}, {0.0f, 1.0f}},
    {{ 0.5f, -0.5f, 0.0f}, {0.0f, 1.0f, 0.0f}, {1.0f, 1.0f}},
    {{ 0.5f,  0.5f, 0.0f}, {0.0f, 0.0f, 1.0f}, {1.0f, 0.0f}},
    {{-0.5f,  0.5f, 0.0f}, {1.0f, 1.0f, 1.0f}, {0.0f, 0.0f}},
}; // 顶点缓冲区

static const std::vector<uint32_t> s_Indices = {
    0, 1, 2,
    2, 3, 0
}; // 索引缓冲区

struct VertexUBO
{
    glm::mat4 model; // 模型矩阵
    glm::mat4 view; // 视图矩阵
    glm::mat4 proj; // 投影矩阵
}; // 顶点着色器的UBO

void TexturedQuadApp::Start(){ // 实现纯虚函数Start
    // pipeline layout 需要 descriptor set layout
    // descriptor set 需要 uniform buffer 已创建
    VKP_INFO("TexturedQuadApp started");

    CreateDescriptorSetLayout(); // 创建描述符集布局
    CreateGraphicsPipeline(); // 创建图形管线

    CreateVertexBuffer(); // 创建顶点缓冲区
    CreateIndexBuffer(); // 创建索引缓冲区
    
    CreateUniformBuffers(); // 创建统一缓冲区
    CreateTexture(); // 创建纹理

    CreateDescriptorPool(); // 创建描述符池
    CreateDescriptorSets(); // 创建描述符集
}

void TexturedQuadApp::ShutdownApp(){ // 实现纯虚函数ShutdownApp
    // pipeline layout 依赖 descriptor set layout 信息
    // descriptor sets 属于 descriptor pool
    // buffer 必须早于 device 销毁
    m_GraphicsPipeline.reset();

    m_DescriptorSets.clear();
    m_DescriptorPool.reset();
    m_DescriptorSetLayout.reset();

    m_Texture.reset();

    m_UniformBuffers.clear();

    m_IndexBuffer.reset();
    m_VertexBuffer.reset();
}

void TexturedQuadApp::CreateDescriptorSetLayout(){ // 创建描述符集布局
    m_DescriptorSetLayout = vkp::DescriptorSetLayout::Builder(GetDevice())
        .AddBinding(
            0,
            VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
            VK_SHADER_STAGE_VERTEX_BIT
        )
        .AddBinding(
            1,
            VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            VK_SHADER_STAGE_FRAGMENT_BIT
        )
        .Build(); // 创建描述符集布局,这里是一个UBO
}

void TexturedQuadApp::CreateGraphicsPipeline(){ // 创建图形管线
    vkp::PipelineConfig config; // 配置

    config.descriptorSetLayouts = {*m_DescriptorSetLayout}; // 描述符集布局
    
    auto bindingDescription = Vertex::GetBindingDescription(); // 绑定描述
    auto attributeDescriptions = Vertex::GetAttributeDescriptions(); // 属性描述

    config.bindingDescriptions = {bindingDescription}; // 绑定描述
    config.attributeDescriptions = {
        attributeDescriptions[0],
        attributeDescriptions[1],
        attributeDescriptions[2]
    }; // 属性描述

    m_GraphicsPipeline = std::make_unique<vkp::Pipeline>(
        GetDevice(),
        GetRenderPass(),
        "shaders/stage4_textured_quad.vert.spv",
        "shaders/stage4_textured_quad.frag.spv",
        config
    );
}

void TexturedQuadApp::CreateVertexBuffer(){ // 创建顶点缓冲区
    VkDeviceSize bufferSize = sizeof(s_Vertices[0]) * s_Vertices.size();

    vkp::Buffer stagingBuffer(
        GetPhysicalDevice(),
        GetDevice(),
        bufferSize,
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
    ); // HOST_VISIBLE | HOST_COHERENT 表示可以从CPU访问

    stagingBuffer.Map(); // 映射内存
    stagingBuffer.CopyToMapped(s_Vertices.data(), bufferSize); // 复制数据，把顶点数据从 CPU 搬进了 host-visible 内存
    stagingBuffer.Unmap(); // 取消映射

    m_VertexBuffer = std::make_unique<vkp::Buffer>(
        GetPhysicalDevice(),
        GetDevice(),
        bufferSize,
        VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT
    ); // DEVICE_LOCAL 表示只能从GPU访问 TRANSFER_DST_BIT 表示可以从CPU传输到GPU

    GetCommandPool().CopyBuffer(
        GetDevice(),
        GetDevice().GetGraphicsQueue(),
        stagingBuffer,
        *m_VertexBuffer,
        bufferSize
    ); // 复制缓冲区
}

void TexturedQuadApp::CreateIndexBuffer(){ // 创建索引缓冲区
    VkDeviceSize bufferSize = sizeof(s_Indices[0]) * s_Indices.size();

    vkp::Buffer stagingBuffer(
        GetPhysicalDevice(),
        GetDevice(),
        bufferSize,
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
    );

    stagingBuffer.Map();
    stagingBuffer.CopyToMapped(s_Indices.data(), bufferSize);
    stagingBuffer.Unmap();

    m_IndexBuffer = std::make_unique<vkp::Buffer>(
        GetPhysicalDevice(),
        GetDevice(),
        bufferSize,
        VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT
    );

    GetCommandPool().CopyBuffer(
        GetDevice(),
        GetDevice().GetGraphicsQueue(),
        stagingBuffer,
        *m_IndexBuffer,
        bufferSize
    );

    m_IndexCount = static_cast<uint32_t>(s_Indices.size());
}

void TexturedQuadApp::CreateUniformBuffers(){ // 创建统一缓冲区
    VkDeviceSize bufferSize = sizeof(VertexUBO);
    
    m_UniformBuffers.clear();
    m_UniformBuffers.reserve(GetMaxFramesInFlight());

    for(uint32_t i = 0; i < GetMaxFramesInFlight(); i++){
        auto uniformBuffer = std::make_unique<vkp::Buffer>(
            GetPhysicalDevice(),
            GetDevice(),
            bufferSize,
            VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
        ); 
        // HOST_VISIBLE | HOST_COHERENT 表示可以从CPU访问
        // 顶点/索引缓冲通常是静态数据，追求最高的 GPU 读取速度，值得花一次拷贝的代价搬到 DEVICE_LOCAL 显存
        // 而 UBO 是动态数据（每帧的 MVP 矩阵、光照参数等），需要频繁更新，所以选择 HOST_VISIBLE | HOST_COHERENT

        uniformBuffer->Map(); // 映射内存 UBO 每帧常驻 mapped。Buffer::~Buffer() 会 Unmap()，所以无需每帧 unmap

        m_UniformBuffers.push_back(std::move(uniformBuffer)); // 移动语义
    }
}

void TexturedQuadApp::CreateDescriptorPool(){ // 创建描述符池
    m_DescriptorPool = vkp::DescriptorPool::Builder(GetDevice())
    .SetMaxSets(GetMaxFramesInFlight())
    .AddPoolSize(
        VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
        GetMaxFramesInFlight()
    )
    .AddPoolSize(
        VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        GetMaxFramesInFlight()
    )
    .Build();
}

void TexturedQuadApp::CreateDescriptorSets(){ // 创建描述符集
    m_DescriptorSets.resize(GetMaxFramesInFlight());

    //  3 个 UBO（对应 3 个飞行帧），每个 UBO 对应一个描述符集
    for(uint32_t i = 0; i < GetMaxFramesInFlight(); i++){
        VkDescriptorBufferInfo bufferInfo{};
        bufferInfo.buffer = *m_UniformBuffers[i];
        bufferInfo.offset = 0;
        bufferInfo.range = sizeof(VertexUBO);

        VkDescriptorImageInfo imageInfo = m_Texture->GetDescriptorInfo();

        bool success = vkp::DescriptorWriter(*m_DescriptorSetLayout, *m_DescriptorPool)
            .WriteBuffer(0, &bufferInfo)
            .WriteImage(1, &imageInfo)
            .Build(m_DescriptorSets[i]);

        if(!success){
            throw std::runtime_error("Failed to allocate descriptor set!");
        }
    } 
}

void TexturedQuadApp::CreateTexture(){ // 创建纹理
    m_Texture = std::make_unique<vkp::Texture2D>(
        GetPhysicalDevice(),
        GetDevice(),
        GetCommandPool(),
        GetDevice().GetGraphicsQueue(),
        "assets/checker.jpeg"
    );
}


void TexturedQuadApp::UpdateUniformBuffers(){ // 更新统一缓冲区
    VertexUBO ubo{};
    ubo.model = glm::rotate(
        glm::mat4(1.0f),
        m_Time,
        glm::vec3(0.0f, 0.0f, 1.0f)
    ); // 旋转矩阵

    ubo.view = m_Camera.GetViewMatrix(); // 视图矩阵

    VkExtent2D extent = GetSwapChain().GetExtent(); // 获取交换链的大小
    float aspect = static_cast<float>(extent.width) / static_cast<float>(extent.height); // 宽高比

    ubo.proj = m_Camera.GetProjectionMatrix(aspect); // 透视投影矩阵

    m_UniformBuffers[GetCurrentFrameIndex()]->CopyToMapped(&ubo, sizeof(ubo)); // 复制数据

}

void TexturedQuadApp::Update(core::Timestep timestep){ // 实现纯虚函数Update
    m_Time += timestep.GetSeconds(); // 时间累加

    float distance = 5.0f * timestep.GetSeconds(); // 计算距离

    if(m_Keys[GLFW_KEY_W]){ // 按下W键
        m_Camera.MoveForward(distance); // 向前移动
    }

    if(m_Keys[GLFW_KEY_S]){ // 按下S键
        m_Camera.MoveForward(-distance); // 向后移动
    }

    if(m_Keys[GLFW_KEY_A]){ // 按下A键
        m_Camera.MoveRight(-distance); // 向左移动
    }

    if(m_Keys[GLFW_KEY_D]){ // 按下D键
        m_Camera.MoveRight(distance); // 向右移动
    }

    if(m_Keys[GLFW_KEY_SPACE]){ // 按下空格键
        m_Camera.MoveUp(distance); // 向上移动
    }

    if(m_Keys[GLFW_KEY_LEFT_CONTROL]){ // 按下左Ctrl键
        m_Camera.MoveUp(-distance); // 向下移动
    }

    m_TitleUpdateTimer += timestep.GetSeconds(); // 标题更新计时器累加

    if(m_TitleUpdateTimer >= 0.5f){ // 每0.5秒更新一次标题
        m_TitleUpdateTimer = 0.0f; // 重置计时器
        
        const glm::vec3& pos = m_Camera.GetPosition(); // 获取相机位置

        std::ostringstream title; // 格式化字符串
        title << "Stage 4 - TexturedQuad | pos=(" 
            << pos.x << ", " 
            << pos.y << ", " 
            << pos.z << ")"
            << " yaw=" << m_Camera.GetYaw()
            << " pitch=" << m_Camera.GetPitch(); // 格式化字符串

        GetWindow().SetTitle(title.str()); // 设置窗口标题
    }

    UpdateUniformBuffers(); // 更新统一缓冲区
}

void TexturedQuadApp::Render(VkCommandBuffer commandBuffer, uint32_t imageIndex){ // 实现纯虚函数Render
    VkCommandBufferBeginInfo beginInfo{}; // 命令缓冲区开始信息
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO; // 结构体类型
    
    if(vkBeginCommandBuffer(commandBuffer, &beginInfo) != VK_SUCCESS){ // 开始命令缓冲区
        throw std::runtime_error("Failed to begin recording command buffer!"); // 失败
    }

    VkRenderPassBeginInfo renderPassInfo{}; // 渲染通道开始信息
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO; // 结构体类型
    renderPassInfo.renderPass = GetRenderPass(); // 获取渲染通道
    renderPassInfo.framebuffer = GetSwapChain().GetFramebuffer(imageIndex); // 获取帧缓冲区
    renderPassInfo.renderArea.offset = {0, 0}; // 渲染区域偏移
    renderPassInfo.renderArea.extent = GetSwapChain().GetExtent(); // 渲染区域大小

    float green = 0.02f + 0.2f * std::abs(std::sin(m_Time)); // 计算绿色分量

    VkClearValue clearColor = {{{0.02f, green, 0.03f, 1.0f}}}; // 清除颜色, 动态变化

    renderPassInfo.clearValueCount = 1; // 清除值数量
    renderPassInfo.pClearValues = &clearColor; // 清除值数组

    vkCmdBeginRenderPass(commandBuffer, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE); // 开始渲染通道
    
    VkViewport viewport{};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = static_cast<float>(GetSwapChain().GetExtent().width);
    viewport.height = static_cast<float>(GetSwapChain().GetExtent().height);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(commandBuffer, 0, 1, &viewport); // 设置视口

    VkRect2D scissor{};
    scissor.offset = {0, 0};
    scissor.extent = GetSwapChain().GetExtent();
    vkCmdSetScissor(commandBuffer, 0, 1, &scissor); // 设置裁剪矩形

    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, *m_GraphicsPipeline); // 绑定管线

    uint32_t currentFrame = GetCurrentFrameIndex();

    // 将包含 MVP 矩阵的 UBO 绑定到着色器，让着色器知道该去读取哪块缓冲区中的变换数据
    vkCmdBindDescriptorSets(
        commandBuffer,
        VK_PIPELINE_BIND_POINT_GRAPHICS,
        m_GraphicsPipeline->GetLayout(),
        0,
        1,
        &m_DescriptorSets[currentFrame],
        0,
        nullptr
    ); // 绑定描述符集
    
    VkBuffer vertexBuffers[] = {*m_VertexBuffer}; // 顶点缓冲区
    VkDeviceSize offsets[] = {0}; // 偏移量

    vkCmdBindVertexBuffers(commandBuffer, 0, 1, vertexBuffers, offsets); // 绑定顶点缓冲区
    vkCmdBindIndexBuffer(commandBuffer, *m_IndexBuffer, 0, VK_INDEX_TYPE_UINT32); // 绑定索引缓冲区

    vkCmdDrawIndexed(commandBuffer, m_IndexCount, 1, 0, 0, 0); // 绘制索引图元
    
    vkCmdEndRenderPass(commandBuffer); // 结束渲染通道

    if(vkEndCommandBuffer(commandBuffer) != VK_SUCCESS){ // 结束命令缓冲区
        throw std::runtime_error("Failed to record command buffer!"); // 失败
    }
}

void TexturedQuadApp::OnFramebufferResize(int width, int height){
    core::Application::OnFramebufferResize(width, height);
    VKP_INFO("Framebuffer resized: {} x {}", width, height);
}

void TexturedQuadApp::OnKey(int key, int scancode, int action, int mods){
    core::Application::OnKey(key, scancode, action, mods);

    if(key < 0 || key >= 1024){
        return;
    }

    if(action == GLFW_PRESS){
        m_Keys[key] = true; // 按下
    }
    else if(action == GLFW_RELEASE){
        m_Keys[key] = false; // 释放
    }
}

void TexturedQuadApp::OnMouseMove(double x, double y){
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
    ); // 鼠标移动
}

void TexturedQuadApp::OnMouseButton(int button, int action, int mods){
    core::Application::OnMouseButton(button, action, mods);
}

