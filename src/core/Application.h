#pragma once

#include <memory>
#include <vector>

#include "core/Timestep.h"
#include "core/Timer.h"
#include "core/Window.h"

#include "vulkan/CommandBuffer.h"
#include "vulkan/CommandPool.h"
#include "vulkan/Device.h"
#include "vulkan/Instance.h"
#include "vulkan/PhysicalDevice.h"
#include "vulkan/RenderPass.h"
#include "vulkan/Surface.h"
#include "vulkan/SwapChain.h"
#include "vulkan/SyncObjects.h"

namespace core
{
class Application
{
public:
    Application();
    virtual ~Application();

    void Run(); // Main loop

protected:
    virtual void Start() = 0; // 纯虚函数，子类必须实现
    virtual void Update(Timestep timestep) = 0; // 更新
    virtual void Render(VkCommandBuffer commandBuffer, uint32_t imageIndex) = 0; // 渲染、
    virtual void PrepareFrame(uint32_t frameIndex, uint32_t imageIndex); // 准备帧

    virtual void OnFramebufferResize(int width, int height); // 窗口大小改变回调
    virtual void OnMouseMove(double x, double y); // 鼠标移动回调
    virtual void OnMouseButton(int button, int action, int mods); // 鼠标按钮回调
    virtual void OnKey(int key, int scancode, int action, int mods); // 键盘回调

    virtual void ShutdownApp(); // 关闭应用程序

protected:
    Window& GetWindow();                    // 获取窗口
    vkp::Instance& GetInstance();           // 获取 Vulkan 实例
    vkp::Device& GetDevice();               // 获取设备
    vkp::SwapChain& GetSwapChain();         // 获取交换链
    vkp::RenderPass& GetRenderPass();       // 获取渲染通道
    vkp::PhysicalDevice& GetPhysicalDevice(); // 获取物理设备
    vkp::CommandPool& GetCommandPool(); // 获取命令池

    uint32_t GetCurrentFrameIndex() const { return m_CurrentFrame; } // 获取当前帧索引
    // static 这是一个类级别的函数，不属于任何对象实例 constexpr 该函数在编译期就可以计算出结果
    static constexpr uint32_t GetMaxFramesInFlight() { return MAX_FRAMES_IN_FLIGHT; } // 获取最大并发帧数量


private:
    void Init();                            // 初始化
    void SetupWindow();                     // 设置窗口
    void SetupVulkan();                     // 设置Vulkan
    void Loop();                            // 主循环
    void Shutdown();                        // 关闭

    void CreateRenderPass();                // 创建渲染通道
    void CreateSwapChain();                 // 创建交换链
    void CreateCommandPool();               // 创建命令池
    void CreateCommandBuffers();            // 创建命令缓冲区
    void CreateSyncObjects();               // 创建同步对象
    void CreateRenderFinishedSemaphoresByImage(); // 为每个 swapchain image 创建 render-finished semaphore
    void DestroyRenderFinishedSemaphoresByImage(); // 销毁每图像 render-finished semaphore

    void DrawFrame();                       // 绘制帧
    void RecordCommandBuffer(VkCommandBuffer commandBuffer, uint32_t imageIndex); // 记录命令缓冲区
    void RecreateSwapChain();               // 重新创建交换链
    void CleanupSwapChain();                // 清理交换链

private:
    static constexpr uint32_t MAX_FRAMES_IN_FLIGHT = 3; // 最大并发帧数量

    std::unique_ptr<Window> m_Window;       // 窗口

    std::unique_ptr<vkp::Instance> m_Instance;              // Vulkan 实例
    std::unique_ptr<vkp::Surface> m_Surface;                // 窗口表面
    std::unique_ptr<vkp::PhysicalDevice> m_PhysicalDevice;  // 物理设备
    std::unique_ptr<vkp::Device> m_Device;                  // 逻辑设备
    std::unique_ptr<vkp::RenderPass> m_RenderPass;          // 渲染通道
    std::unique_ptr<vkp::SwapChain> m_SwapChain;            // 交换链
    std::unique_ptr<vkp::CommandPool> m_CommandPool;        // 命令池
    std::vector<std::unique_ptr<vkp::CommandBuffer>> m_CommandBuffers; // 命令缓冲区
    std::vector<std::unique_ptr<vkp::FrameSyncObjects>> m_FrameSyncObjects; // 同步对象
    std::vector<VkSemaphore> m_RenderFinishedSemaphoresByImage; // 每个 swapchain image 一个 present 等待 semaphore

    uint32_t m_CurrentFrame = 0; // 当前帧索引
    bool m_FramebufferResized = false; // 帧缓冲区大小是否改变
    bool m_ShutdownCalled = false; // 是否调用了关闭函数
    VkFormat m_DepthFormat = VK_FORMAT_UNDEFINED; // 深度格式
};
}