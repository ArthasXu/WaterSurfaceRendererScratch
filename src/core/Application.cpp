#include "core/Application.h"

#include "core/Log.h"

#include <GLFW/glfw3.h>

#include <iostream>
#include <stdexcept>

namespace core
{
Application::Application() = default; // 默认构造函数

Application::~Application() = default; // 默认析构函数

void Application::Run(){ // 主循环
    try{
        Init(); // 初始化
        Loop(); // 主循环
        Shutdown(); // 关闭
    }
    catch(const std::exception& e){
        Shutdown(); // 关闭
        std::cerr << e.what() << "\n"; // 输出异常信息
        throw; // 抛出异常
    }
}

void Application::Init() { // 初始化
    Log::Init(); // 初始化日志
    SetupWindow(); // 设置窗口
    SetupVulkan(); // 设置Vulkan
    Start(); // 启动    
}

void Application::SetupWindow() { // 设置窗口
    if(!glfwInit()) { // 初始化GLFW
         throw std::runtime_error("Failed to initialize GLFW"); 
    }

    m_Window = std::make_unique<Window>(1280, 720, "Stage 3 - Clear Color"); // 创建窗口

    m_Window->OnFramebufferResize = [this](int width, int height){ // 设置窗口大小改变回调
        OnFramebufferResize(width, height); // 调用窗口大小改变回调
    };

    m_Window->OnMouseMove = [this](double xpos, double ypos){ // 设置鼠标移动回调
        OnMouseMove(xpos, ypos); // 调用鼠标移动回调
    };

    m_Window->OnMouseButton = [this](int button, int action, int mods){ // 设置鼠标按钮回调
        OnMouseButton(button, action, mods); // 调用鼠标按钮回调    
    };

    m_Window->OnKey = [this](int key, int scancode, int action, int mods){ // 设置键盘回调
        OnKey(key, scancode, action, mods); // 调用键盘回调    
    };
}

void Application::SetupVulkan() { // 设置Vulkan
#ifdef NDEBUG
    const bool enableValidationLayers = false; // 不启用验证层
#else
    const bool enableValidationLayers = true; // 启用验证层
#endif

    m_Instance = std::make_unique<vkp::Instance>(enableValidationLayers); // 创建实例
    m_Surface = std::make_unique<vkp::Surface>(*m_Instance, m_Window->GetNativeWindow()); // 创建表面
    m_PhysicalDevice = std::make_unique<vkp::PhysicalDevice>(*m_Instance, *m_Surface); // 选择物理设备
    m_Device = std::make_unique<vkp::Device>(*m_PhysicalDevice, m_PhysicalDevice->GetQueueFamilyIndices()); // 创建逻辑设备

    CreateRenderPass(); // 创建渲染通道
    CreateSwapChain(); // 创建交换链
    CreateCommandPool(); // 创建命令池
    CreateCommandBuffers(); // 创建命令缓冲区
    CreateSyncObjects(); // 创建同步对象
}

void Application::CreateRenderPass() { // 创建渲染通道
    vkp::SwapChainSupportDetails swapChainSupport = m_PhysicalDevice->QuerySwapChainSupport(); // 查询交换链支持信息
    VkFormat colorFormat = vkp::SwapChain::chooseSwapSurfaceFormat(swapChainSupport.formats).format; // 选择交换链图像格式

    m_RenderPass = std::make_unique<vkp::RenderPass>(*m_Device, colorFormat); // 创建渲染通道
}

void Application::CreateSwapChain() { // 创建交换链
    m_SwapChain = std::make_unique<vkp::SwapChain>(
        *m_PhysicalDevice,
        *m_Device,
        *m_Surface,
        m_Window->GetNativeWindow(), // 获取原生窗口
        m_PhysicalDevice->GetQueueFamilyIndices(), // 获取队列族索引
        *m_RenderPass
    );
}

void Application::CreateCommandPool() { // 创建命令池
    m_CommandPool = std::make_unique<vkp::CommandPool>(
        *m_Device, 
        m_Device->GetGraphicsQueueFamily()
    ); // 创建命令池    
}

void Application::CreateCommandBuffers() { // 创建命令缓冲区
    m_CommandBuffers.clear();
    m_CommandBuffers.reserve(MAX_FRAMES_IN_FLIGHT); // 预留空间

    for(uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++){ // 创建命令缓冲区
        m_CommandBuffers.push_back(
            std::make_unique<vkp::CommandBuffer>(*m_Device, *m_CommandPool)
        );
    }
}

void Application::CreateSyncObjects() { // 创建同步对象
    m_FrameSyncObjects.clear();
    m_FrameSyncObjects.reserve(MAX_FRAMES_IN_FLIGHT); // 预留空间
    
    for(uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++){ // 创建同步对象
        m_FrameSyncObjects.push_back(
            std::make_unique<vkp::FrameSyncObjects>(*m_Device)
        );
    }
}

void Application::Loop() { // 主循环
    Timer timer; // 计时器
    
    while(!m_Window->ShouldClose()){ // 循环直到窗口关闭
        m_Window->PollEvents(); // 处理事件
        
        Timestep timestep = timer.Tick(); // 计时, 返回时间间隔

        Update(timestep); // 更新
        DrawFrame(); // 绘制帧
    }

    vkDeviceWaitIdle(*m_Device); // 等待设备空闲
}

void Application::DrawFrame() { // 绘制帧
    auto& sync = *m_FrameSyncObjects[m_CurrentFrame]; // 获取当前帧的同步对象
    VkFence inFlightFence = sync.InFlightFence(); // 获取当前帧的栅栏

    vkWaitForFences(*m_Device, 1, &inFlightFence, VK_TRUE, UINT64_MAX); // 等待栅栏

    uint32_t imageIndex = 0; // 图像索引
    VkResult result = m_SwapChain->AcquireNextImage(sync.ImageAvailable(), &imageIndex); // 等待图像可用

    if(result == VK_ERROR_OUT_OF_DATE_KHR){ // 交换链已过期
        RecreateSwapChain(); // 重新创建交换链
        return;
    }
    else if(result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR){ // 其他错误
        throw std::runtime_error("Failed to acquire swap chain image!"); // 失败
    }

    vkResetFences(*m_Device, 1, &inFlightFence); // 重置栅栏

    m_CommandBuffers[m_CurrentFrame]->Reset(); // 重置命令缓冲区
    RecordCommandBuffer(*m_CommandBuffers[m_CurrentFrame], imageIndex); // 记录命令缓冲区

    VkSemaphore waitSemaphores[] = {sync.ImageAvailable()}; // 等待信号量
    VkPipelineStageFlags waitStages[] = {VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT}; // 等待阶段
    VkSemaphore signalSemaphores[] = {sync.RenderFinished()}; // 信号量

    VkCommandBuffer commandBuffer = *m_CommandBuffers[m_CurrentFrame]; // 命令缓冲区

    VkSubmitInfo submitInfo{}; // 提交信息
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO; // 结构体类型
    submitInfo.waitSemaphoreCount = 1; // 等待信号量数量
    submitInfo.pWaitSemaphores = waitSemaphores; // 等待信号量数组
    submitInfo.pWaitDstStageMask = waitStages; // 等待阶段数组
    submitInfo.commandBufferCount = 1; // 命令缓冲区数量
    submitInfo.pCommandBuffers = &commandBuffer; // 命令缓冲区数组
    submitInfo.signalSemaphoreCount = 1; // 信号量数量
    submitInfo.pSignalSemaphores = signalSemaphores; // 信号量数组

    if(vkQueueSubmit(m_Device->GetGraphicsQueue(), 1, &submitInfo, inFlightFence) != VK_SUCCESS){ // 提交命令缓冲区
        throw std::runtime_error("Failed to submit draw command buffer!"); // 失败    
    }

    result = m_SwapChain->Present(m_Device->GetPresentQueue(), sync.RenderFinished(), imageIndex); // 呈现图像

    if(result == VK_ERROR_OUT_OF_DATE_KHR || // 交换链已过期
        result == VK_SUBOPTIMAL_KHR || // 交换链已被调整大小或不支持的格式
        m_FramebufferResized){ // 帧缓冲区已调整大小
        m_FramebufferResized = false; // 重置帧缓冲区已调整大小标志
        RecreateSwapChain(); // 重新创建交换链
    }    
    else if(result != VK_SUCCESS){ // 其他错误
        throw std::runtime_error("Failed to present swap chain image!"); // 失败
    }

    m_CurrentFrame = (m_CurrentFrame + 1) % MAX_FRAMES_IN_FLIGHT; // 更新当前帧索引
}

void Application::RecordCommandBuffer(VkCommandBuffer commandBuffer, uint32_t imageIndex) { // 记录命令缓冲区
    Render(commandBuffer, imageIndex);
}

void Application::RecreateSwapChain(){ // 重新创建交换链
    VkExtent2D extent = m_Window->GetFramebufferExtent(); // 获取帧缓冲区的尺寸

    while(extent.width == 0 || extent.height == 0){ // 帧缓冲区尺寸为 0
        glfwWaitEvents(); // 等待事件
        extent = m_Window->GetFramebufferExtent(); // 获取帧缓冲区的尺寸
    }

    vkDeviceWaitIdle(*m_Device); // 等待设备空闲

    CleanupSwapChain(); // 清理交换链

    CreateRenderPass(); // 创建渲染通道
    CreateSwapChain(); // 创建交换链
}

void Application::CleanupSwapChain(){ // 清理交换链
    m_SwapChain.reset(); // unique_ptr 析构 SwapChain 并销毁 framebuffers/imageViews/swapchain
    m_RenderPass.reset(); // unique_ptr 析构 RenderPass 并销毁 VkRenderPass
}

void Application::ShutdownApp(){
}

void Application::Shutdown()
{
    if(m_ShutdownCalled){ // 已经调用过关闭函数
        return;
    }
    m_ShutdownCalled = true; // 标记已经调用过关闭函数

    if(m_Device){
        vkDeviceWaitIdle(*m_Device);
    }

    ShutdownApp();

    CleanupSwapChain();

    m_FrameSyncObjects.clear();
    m_CommandBuffers.clear();
    m_CommandPool.reset();

    m_Device.reset();
    m_PhysicalDevice.reset();
    m_Surface.reset();
    m_Instance.reset();

    m_Window.reset();
    glfwTerminate();
}

void Application::OnFramebufferResize(int width, int height)
{
    m_FramebufferResized = true;
}

void Application::OnMouseMove(double x, double y)
{
}

void Application::OnKey(int key, int scancode, int action, int mods)
{
    if(key == GLFW_KEY_ESCAPE && action == GLFW_PRESS){
        glfwSetWindowShouldClose(m_Window->GetNativeWindow(), GLFW_TRUE);
    }
}

void Application::OnMouseButton(int button, int action, int mods)
{

}

Window& Application::GetWindow()
{
    return *m_Window;
}

vkp::Device& Application::GetDevice()
{
    return *m_Device;
}

vkp::SwapChain& Application::GetSwapChain()
{
    return *m_SwapChain;
}

vkp::RenderPass& Application::GetRenderPass()
{
    return *m_RenderPass;
}

vkp::PhysicalDevice& Application::GetPhysicalDevice()
{
    return *m_PhysicalDevice;
}

vkp::CommandPool& Application::GetCommandPool()
{
    return *m_CommandPool;
}

}