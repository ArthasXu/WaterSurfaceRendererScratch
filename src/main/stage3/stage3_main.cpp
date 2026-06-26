#include <cstring>
#include <iostream>
#include <vector>
#include <cstdlib>
#include <stdexcept>
#include <optional>
#include <set> 
#include <algorithm>    // clamp
#include <cstdint>      // uint32_t
#include <limits>       // UINT32_MAX
#include <fstream>      // 二进制读取 .spv
#include <memory>       // unique_ptr
#include <thread>       // std::thread

#include <vulkan/vulkan.h>
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>

#include "vulkan/Instance.h"
#include "vulkan/Surface.h"
#include "vulkan/PhysicalDevice.h"
#include "vulkan/Device.h"
#include "vulkan/RenderPass.h"
#include "vulkan/Pipeline.h"
#include "vulkan/SwapChain.h"
#include "vulkan/CommandPool.h"
#include "vulkan/CommandBuffer.h"
#include "vulkan/SyncObjects.h"
#include "vulkan/Buffer.h"

#include "core/Timestep.h"
#include "core/Timer.h"
#include "core/Log.h"
#include "core/Assert.h"
#include "core/Profile.h"


GLFWwindow* g_Window = nullptr;

// ------------------ Instance → Present 对象依赖图 -------------------------
// VkInstance
//   ├── VkDebugUtilsMessengerEXT（调试信使，可选）
//   ├── VkSurfaceKHR（窗口表面）
//   │     └── 由 GLFW 窗口创建，依赖 VkInstance 和平台扩展
//   ├── VkPhysicalDevice（物理设备列表）
//   │     └── 用于查询队列族、表面支持、特性等
//   └── VkDevice（逻辑设备）
//         ├── VkQueue（图形队列）
//         ├── VkQueue（呈现队列）
//         ├── VkSwapchainKHR（交换链）
//         │     ├── 依赖 VkDevice 和 VkSurfaceKHR
//         │     ├── VkImage[]（交换链图像）
//         │     └── VkImageView[]（图像视图）
//         │           └── 每个视图对应一张交换链图像
//         ├── VkRenderPass
//         │     └── 描述附件格式、加载/存储操作、子过程布局转换
//         ├── VkPipelineLayout（管线布局，描述资源绑定）
//         ├── VkPipeline（图形管线）
//         │     └── 绑定到 VkRenderPass 的特定子过程
//         ├── VkFramebuffer[]（帧缓冲区）
//         │     └── 每个帧缓冲区关联一个 VkImageView（颜色附件），并与 VkRenderPass 兼容
//         ├── VkCommandPool（命令池，属于图形队列族）
//         │     └── VkCommandBuffer[]（命令缓冲区，每飞行帧一个）
//         └── 同步对象（每飞行帧一组）,飞行帧是指在同一时间内正在处理的帧
//               ├── VkSemaphore（imageAvailableSemaphore）
//               ├── VkSemaphore（renderFinishedSemaphore）
//               └── VkFence（inFlightFence）
// ------------------ Instance → Present 对象依赖图 -------------------------
std::unique_ptr<vkp::Instance> g_Instance;                                  // Vulkan 实例
std::unique_ptr<vkp::Surface> g_Surface;                                    // 窗口表面，用于呈现图像到窗口
std::unique_ptr<vkp::PhysicalDevice> g_PhysicalDevice;                      // 物理设备，GPU
std::unique_ptr<vkp::Device> g_Device;                                      // 逻辑设备，用于执行 Vulkan 命令
std::unique_ptr<vkp::RenderPass> g_RenderPass;                              // 渲染通道，用于描述渲染过程
std::unique_ptr<vkp::Pipeline> g_GraphicsPipeline;                          // 图形管线，用于描述图形渲染过程
std::unique_ptr<vkp::SwapChain> g_SwapChain;                                // 交换链资源，用于管理呈现到屏幕的图像缓冲区序列
std::unique_ptr<vkp::CommandPool> g_CommandPool;                            // 命令池，用于分配和管理命令缓冲区
std::vector<std::unique_ptr<vkp::CommandBuffer>> g_CommandBuffers;          // 命令缓冲区，用于记录和提交 Vulkan 命令
std::vector<std::unique_ptr<vkp::FrameSyncObjects>> g_FrameSyncObjects;     // 同步对象，用于控制命令缓冲区的执行顺序

const int MAX_FRAMES_IN_FLIGHT = 3;                             // 最大并发帧数量，用于防止命令缓冲区重叠
uint32_t g_CurrentFrame = 0;                                    // 当前帧索引，用于标识当前正在处理的帧
bool g_FramebufferResized = false;                              // 帧缓冲区大小是否改变，用于标识帧缓冲区是否需要重新创建

const std::vector<const char*> g_ValidationLayers = {
    "VK_LAYER_KHRONOS_validation"
};

#ifdef NDEBUG
const bool g_EnableValidationLayers = false;
#else
const bool g_EnableValidationLayers = true;
#endif

void initWindow();  // 初始化窗口
void initVulkan();  // 初始化 Vulkan
void mainLoop();    // 主循环
void cleanup();     // 清理资源


void createSwapChain();         // 创建交换链
void createRenderPass();        // 创建渲染通道
void createGraphicsPipeline();  // 创建图形管线

void createCommandPool();       // 创建命令池
void createCommandBuffers();    // 创建命令缓冲区
void createSyncObjects();       // 创建同步对象

void drawFrame();               // 绘制帧
void recordCommandBuffer(VkCommandBuffer commandBuffer, uint32_t imageIndex);   // 记录命令缓冲区

void framebufferResizeCallback(GLFWwindow* window, int width, int height); // 窗口大小改变回调

void recreateSwapChain();       // 重新创建交换链
void cleanupSwapChain();        // 清理交换链

int main(){
    try{
        initWindow();
        initVulkan();
        mainLoop();
        cleanup();

        VKP_PROFILE_SCOPE("Stage3Main");
        VKP_ASSERT(true, "Assert should not fail");

        return 0;
    }
    catch(const std::exception& e){
        std::cerr << e.what() << '\n';
        cleanup();
        return 1;
    }
}

void initWindow(){ // 初始化窗口
    if (!glfwInit())
    {
        std::cerr << "Failed to initialize GLFW\n";
        std::exit(1);
    }

    // void glfwWindowHint(int hint, int value);
    // hint：要设置的窗口属性。
    // value：属性的值。
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);

    // GLFWwindow* glfwCreateWindow(
    //     int width,                // 窗口客户区宽度（像素）
    //     int height,               // 窗口客户区高度（像素）
    //     const char* title,        // 窗口标题（UTF-8 字符串）
    //     GLFWmonitor* monitor,     // 要在哪个监视器上全屏，或 nullptr 表示窗口模式
    //     GLFWwindow* share         // 要与哪个窗口共享 OpenGL 上下文，或 nullptr
    // );
    g_Window = glfwCreateWindow(1280, 720, "Stage 1 - Vulkan Triangle", nullptr, nullptr);
    if (!g_Window)
    {
        std::cerr << "Failed to create GLFW window\n";
        glfwTerminate();
        std::exit(1);
    }

    glfwSetFramebufferSizeCallback(g_Window, framebufferResizeCallback); // 设置窗口大小改变回调
}
void initVulkan(){ // 初始化 Vulkan
    // Instance  ──> Surface ──> Physical Device ──> Logical Device ──> SwapChain
    //                                                                      │
    //                                RenderPass 是规则  RenderPass <── ImageViews
    //                                                      │               │
    //                                                  GraphicsPipeline    │
    //                                                      │               │
    //                                                      └───> Framebuffers  Framebuffer 是实物绑定
    //                                                                      │
    //                                                                  CommandPool 依附于逻辑设备的图形队列族
    //                                                                      │
    //                                                                 CommandBuffers 录制的内容将引用 Framebuffers、RenderPass、GraphicsPipeline 等所有已创建的资源，将它们串联起来形成实际可执行的绘制命令
    //                                                                      │
    //                                                      SyncObjects (Semaphores + Fences) 用于同步命令缓冲区的执行
    //                                RenderPass 定义“怎么渲染”，Framebuffer 定义“渲染到哪里”。
    g_Instance = std::make_unique<vkp::Instance>(g_EnableValidationLayers);             // 创建实例
    g_Surface = std::make_unique<vkp::Surface>(*g_Instance, g_Window);                  // 创建窗口表面
    g_PhysicalDevice = std::make_unique<vkp::PhysicalDevice>(*g_Instance, *g_Surface);  // 选择物理设备
    g_Device = std::make_unique<vkp::Device>(*g_PhysicalDevice, g_PhysicalDevice->GetQueueFamilyIndices()); // 创建逻辑设备

    createRenderPass();         // 创建渲染通道
    createSwapChain();          // 创建交换链
    createGraphicsPipeline();   // 创建图形管线
    createCommandPool();        // 创建命令池
    createCommandBuffers();     // 创建命令缓冲区
    createSyncObjects();        // 创建同步对象

}
void mainLoop(){ // 主循环
    while(!glfwWindowShouldClose(g_Window)){
        glfwPollEvents(); // 处理所有等待中的事件
        
        if(glfwGetKey(g_Window, GLFW_KEY_ESCAPE) == GLFW_PRESS){ // 按下esc键
            glfwSetWindowShouldClose(g_Window, GLFW_TRUE);
        }

        drawFrame(); // 绘制帧
    }

    vkDeviceWaitIdle(*g_Device); // 等待设备空闲
}
void cleanup(){  // 清理资源
    if(g_Device && *g_Device != VK_NULL_HANDLE){ // 确保没有 GPU 命令还在用 framebuffer/pipeline/swapchain
        vkDeviceWaitIdle(*g_Device); // 等待设备空闲
    }

    cleanupSwapChain(); // 清理交换链

    g_FrameSyncObjects.clear(); // unique_ptr 析构 FrameSyncObjects 并销毁 semaphore/fence
    g_CommandBuffers.clear();   // 销毁命令缓冲区
    g_CommandPool.reset();      // 销毁命令缓冲区
    g_Device.reset();           // 销毁逻辑设备, Device 必须早于 Surface/Instance 销毁
    g_PhysicalDevice.reset();   // PhysicalDevice 不拥有 Vulkan 资源，不用销毁。但为顺序清晰，在 device 销毁后 reset
    g_Surface.reset();          // 销毁窗口表面
    g_Instance.reset();         // std::unique_ptr 的 reset() 会先释放其所有权，即调用当前所指向的 vkp::Instance 的析构函数

    if(g_Window != nullptr){
        glfwDestroyWindow(g_Window); // 销毁窗口
    }
    glfwTerminate(); // 终止 GLFW
}

void createRenderPass(){
    vkp::SwapChainSupportDetails swapChainSupport = g_PhysicalDevice->QuerySwapChainSupport(); // 查询交换链支持信息
    VkFormat colorFormat = vkp::SwapChain::chooseSwapSurfaceFormat(swapChainSupport.formats).format; // 选择交换链图像格式
    g_RenderPass = std::make_unique<vkp::RenderPass>(*g_Device, colorFormat); // 创建渲染通道
}
void createSwapChain(){
    g_SwapChain = std::make_unique<vkp::SwapChain>(
        *g_PhysicalDevice,
        *g_Device,
        *g_Surface,
        g_Window,
        g_PhysicalDevice->GetQueueFamilyIndices(),
        *g_RenderPass
    ); // 创建交换链
}
void createGraphicsPipeline(){
    g_GraphicsPipeline = std::make_unique<vkp::Pipeline>(
        *g_Device, 
        *g_RenderPass,
        "shaders/stage1_triangle.vert.spv",
        "shaders/stage1_triangle.frag.spv"
    );
}

void createCommandPool(){ // 创建命令池
    g_CommandPool = std::make_unique<vkp::CommandPool>(
        *g_Device, 
        g_Device->GetGraphicsQueueFamily()
    ); // 创建命令池
}

void createCommandBuffers(){ // 创建命令缓冲区
    g_CommandBuffers.clear(); // 清空命令缓冲区
    g_CommandBuffers.reserve(MAX_FRAMES_IN_FLIGHT); // 预留空间

    for(size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++){ // 创建命令缓冲区
        g_CommandBuffers.push_back(
            std::make_unique<vkp::CommandBuffer>(*g_Device, *g_CommandPool)
        );
    }

    std::cout << "Command buffers: " << g_CommandBuffers.size() << "\n";
}
void createSyncObjects(){ // 创建同步对象
    g_FrameSyncObjects.clear(); // 清空同步对象
    g_FrameSyncObjects.reserve(MAX_FRAMES_IN_FLIGHT); // 预留空间

    for(size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++){ // 创建同步对象
        g_FrameSyncObjects.push_back(
            std::make_unique<vkp::FrameSyncObjects>(*g_Device)
        );
    }

    std::cout << "Sync objects: " << MAX_FRAMES_IN_FLIGHT << " frames\n";
}


void drawFrame(){ // 绘制帧
    // drawFrame()
    // ├── 1. vkWaitForFences
    // │      等待 g_InFlightFences[g_CurrentFrame] 有信号
    // │      → 确保当前飞行帧的 GPU 工作已全部完成，CPU 可以安全重用该帧的资源
    // │
    // ├── 2. vkAcquireNextImageKHR
    // │      从交换链请求下一张可用的图像索引
    // │      信号量：g_ImageAvailableSemaphores[g_CurrentFrame] 将在图像就绪时被触发
    // │      ├── 如果返回 VK_ERROR_OUT_OF_DATE_KHR
    // │      │     └── recreateSwapChain() → return
    // │      └── 如果返回其他错误 (≠ VK_SUCCESS, ≠ VK_SUBOPTIMAL_KHR)
    // │            └── 抛出异常
    // │
    // ├── 3. vkResetFences
    // │      重置 g_InFlightFences[g_CurrentFrame] 为未发信号状态
    // │      → 准备让本次提交再次使用该栅栏
    // │
    // ├── 4. vkResetCommandBuffer
    // │      重置 g_CommandBuffers[g_CurrentFrame] 的内容
    // │      → 清空旧命令，准备录制新一帧的绘制指令
    // │
    // ├── 5. recordCommandBuffer
    // │      向 g_CommandBuffers[g_CurrentFrame] 录制绘制命令
    // │      ├── 设置视口 (vkCmdSetViewport)
    // │      ├── 设置裁剪矩形 (vkCmdSetScissor)
    // │      ├── 开始渲染通道 (vkCmdBeginRenderPass)
    // │      │     ├── 绑定帧缓冲 (对应 imageIndex)
    // │      │     └── 清除颜色附件
    // │      ├── 绑定图形管线 (vkCmdBindPipeline)
    // │      └── 绘制 (vkCmdDraw)
    // │            └── 结束渲染通道 (vkCmdEndRenderPass)
    // │
    // ├── 6. vkQueueSubmit
    // │      将命令缓冲提交给图形队列
    // │      等待信号量：g_ImageAvailableSemaphores[g_CurrentFrame]
    // │      等待阶段：VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT
    // │      发出信号量：g_RenderFinishedSemaphores[g_CurrentFrame]
    // │      栅栏：g_InFlightFences[g_CurrentFrame]（GPU 完成提交后触发）
    // │
    // ├── 7. vkQueuePresentKHR
    // │      将渲染结果提交给呈现队列进行显示
    // │      等待信号量：g_RenderFinishedSemaphores[g_CurrentFrame]
    // │      交换链：g_SwapChain
    // │      图像索引：imageIndex
    // │      ├── 如果返回 VK_ERROR_OUT_OF_DATE_KHR 或 VK_SUBOPTIMAL_KHR
    // │      │     └── g_FramebufferResized = false → recreateSwapChain()
    // │      ├── 如果 g_FramebufferResized 为真
    // │      │     └── g_FramebufferResized = false → recreateSwapChain()
    // │      └── 如果返回其他错误
    // │            └── 抛出异常
    // │
    // └── 8. g_CurrentFrame = (g_CurrentFrame + 1) % MAX_FRAMES_IN_FLIGHT
    //     推进到下一个飞行帧索引，下一轮使用另一组同步对象和命令缓冲
    // VkResult vkWaitForFences(
    //     VkDevice                                    device,      // 设备
    //     uint32_t                                    fenceCount,  // 栅栏数量
    //     const VkFence*                              pFences,     // 栅栏数组
    //     VkBool32                                    waitAll,     // 是否等待所有栅栏
    //     uint64_t                                    timeout      // 超时时间
    // );
    auto& sync = *g_FrameSyncObjects[g_CurrentFrame];
    
    VkFence inFlightFence = sync.InFlightFence();
    
    vkWaitForFences(*g_Device, 1, &inFlightFence, VK_TRUE, UINT64_MAX); // 等待栅栏

    uint32_t imageIndex = 0; // 图像索引
    // VkResult vkAcquireNextImageKHR(
    //     VkDevice                                    device,      // 设备
    //     VkSwapchainKHR                              swapchain,   // 交换链
    //     uint64_t                                    timeout,     // 超时时间
    //     VkSemaphore                                 semaphore,   // 信号量
    //     VkFence                                     fence,       // 栅栏
    //     uint32_t*                                   pImageIndex  // 图像索引
    // ); // 获取下一个图像
    VkResult result = g_SwapChain->AcquireNextImage(sync.ImageAvailable(), &imageIndex); // 等待图像可用

    if(result == VK_ERROR_OUT_OF_DATE_KHR){ // 交换链已过期
        recreateSwapChain(); // 重新创建交换链
        return;    
    }
    else if(result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR){ // 其他错误
        throw std::runtime_error("Failed to acquire swap chain image!"); // 失败
    }

    vkResetFences(*g_Device, 1, &inFlightFence); // 重置栅栏, 防止上一帧的栅栏未完成导致当前帧无法开始

    g_CommandBuffers[g_CurrentFrame]->Reset(); // 重置命令缓冲区, 防止上一帧的命令缓冲区未完成导致当前帧无法开始
    recordCommandBuffer(*g_CommandBuffers[g_CurrentFrame], imageIndex); // 记录命令缓冲区, 更新命令缓冲区

    VkSemaphore waitSemaphores[] = {sync.ImageAvailable()}; // 等待信号量
    VkPipelineStageFlags waitStages[] = {VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT}; // 等待阶段
    VkSemaphore signalSemaphores[] = {sync.RenderFinished()}; // 信号量

    VkSubmitInfo submitInfo{}; // 提交信息
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO; // 结构体类型
    submitInfo.waitSemaphoreCount = 1; // 等待信号量数量
    submitInfo.pWaitSemaphores = waitSemaphores; // 等待信号量数组
    submitInfo.pWaitDstStageMask = waitStages; // 等待阶段数组
    submitInfo.commandBufferCount = 1; // 命令缓冲区数量
    // 1. g_CommandBuffers 是 std::vector<std::unique_ptr<vkp::CommandBuffer>>
    // 2. g_CommandBuffers[g_CurrentFrame] 拿到的是 std::unique_ptr<vkp::CommandBuffer>&（智能指针引用），它内部保存着指向 vkp::CommandBuffer 的原始指针
    // 3. *g_CommandBuffers[g_CurrentFrame] 解引用智能指针，拿到 vkp::CommandBuffer&（你的包装类对象的引用）
    // 4. VkCommandBuffer commandBuffer = *...; —— 隐式类型转换，将 vkp::CommandBuffer& 转换为 VkCommandBuffer
    // 5. VkSubmitInfo::pCommandBuffers 的类型是 const VkCommandBuffer*，它期望一个指向 VkCommandBuffer 的指针数组
    // 6. commandBuffer 是一个 VkCommandBuffer 局部变量，&commandBuffer 是它的地址（类型 VkCommandBuffer*），可以安全地赋值给 pCommandBuffers
    VkCommandBuffer commandBuffer = *g_CommandBuffers[g_CurrentFrame]; // 命令缓冲区
    submitInfo.pCommandBuffers = &commandBuffer; // 命令缓冲区数组
    submitInfo.signalSemaphoreCount = 1; // 信号量数量
    submitInfo.pSignalSemaphores = signalSemaphores; // 信号量数组

    // VkResult vkQueueSubmit(
    //     VkQueue                                     queue,       // 队列
    //     uint32_t                                    submitCount, // 提交数量
    //     const VkSubmitInfo*                         pSubmits,    // 提交信息数组
    //     VkFence                                     fence        // 栅栏
    // ); // 提交命令缓冲区
    if(vkQueueSubmit(g_Device->GetGraphicsQueue(), 1, &submitInfo, inFlightFence) != VK_SUCCESS){ // 提交命令缓冲区
        throw std::runtime_error("Failed to submit draw command buffer!"); // 失败
    }


    
    result = g_SwapChain->Present(g_Device->GetPresentQueue(), sync.RenderFinished(), imageIndex); // 呈现图像

    if(result == VK_ERROR_OUT_OF_DATE_KHR || // 交换链已过期
        result == VK_SUBOPTIMAL_KHR || // 交换链已被调整大小或不支持的格式
        g_FramebufferResized){ // 帧缓冲区已调整大小
        g_FramebufferResized = false; // 重置帧缓冲区已调整大小
        recreateSwapChain(); // 重新创建交换链
    }
    else if(result != VK_SUCCESS){ // 其他错误
        throw std::runtime_error("Failed to present swap chain image!"); // 失败
    }

    g_CurrentFrame = (g_CurrentFrame + 1) % MAX_FRAMES_IN_FLIGHT; // 更新当前帧索引
}
void recordCommandBuffer(VkCommandBuffer commandBuffer, uint32_t imageIndex){ // 记录命令缓冲区
    // begin command buffer
    //         |
    // begin render pass
    //         |
    // set dynamic viewport
    //         |
    // set dynamic scissor
    //         |
    // bind graphics pipeline
    //         |
    // draw 3 vertices
    //         |
    // end render pass
    //         |
    // end command buffer
    // 不能把所有事情都写在 CommandBuffer 里
    // 根本上是因为 CommandBuffer 是动态的执行指令流
    // 而 RenderPass 和 Pipeline 是静态的硬件配置与优化契约
    VkCommandBufferBeginInfo beginInfo{}; // 命令缓冲区开始信息
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO; // 结构体类型

    if(vkBeginCommandBuffer(commandBuffer, &beginInfo) != VK_SUCCESS){ // 开始命令缓冲区
        throw std::runtime_error("Failed to begin recording command buffer!"); // 失败
    }

    VkRenderPassBeginInfo renderPassInfo{}; // 渲染通道开始信息
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO; // 结构体类型
    renderPassInfo.renderPass = *g_RenderPass; // 渲染通道
    renderPassInfo.framebuffer = g_SwapChain->GetFramebuffer(imageIndex); // 帧缓冲区
    renderPassInfo.renderArea.offset = {0, 0}; // 渲染区域偏移
    renderPassInfo.renderArea.extent = g_SwapChain->GetExtent(); // 渲染区域大小

    VkClearValue clearColor = {{{0.02f, 0.02f, 0.03f, 1.0f}}}; // 清除颜色
    renderPassInfo.clearValueCount = 1; // 清除值数量
    renderPassInfo.pClearValues = &clearColor; // 清除值数组

    // void vkCmdBeginRenderPass(
    //     VkCommandBuffer                             commandBuffer,       // 命令缓冲区
    //     const VkRenderPassBeginInfo*                pRenderPassBegin,    // 渲染通道开始信息
    //     VkSubpassContents                           contents             // 子通道内容
    // ); // 开始渲染通道
    vkCmdBeginRenderPass(commandBuffer, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE); // 开始渲染通道

    VkViewport viewport{}; // 视口, 负责坐标系变换
    // 定义裁剪空间后的 NDC 坐标如何映射到帧缓冲像素坐标
    // 即把顶点着色器输出的几何体按 x, y, width, height 和 minDepth, maxDepth 缩放/偏移到实际渲染目标区域
    viewport.x = 0.0f; // 视口 x
    viewport.y = 0.0f; // 视口 y
    viewport.width = static_cast<float>(g_SwapChain->GetExtent().width); // 视口宽度
    viewport.height = static_cast<float>(g_SwapChain->GetExtent().height); // 视口高度
    viewport.minDepth = 0.0f; // 视口最小深度
    viewport.maxDepth = 1.0f; // 视口最大深度
    vkCmdSetViewport(commandBuffer, 0, 1, &viewport); // 设置视口

    VkRect2D scissor{}; // 裁剪矩形, 负责像素丢弃
    scissor.offset = {0, 0}; // 视口偏移
    scissor.extent = g_SwapChain->GetExtent(); // 视口大小
    vkCmdSetScissor(commandBuffer, 0, 1, &scissor); // 设置视口

    // void vkCmdBindPipeline(
    //     VkCommandBuffer                             commandBuffer,       // 命令缓冲区
    //     VkPipelineBindPoint                         pipelineBindPoint,   // 管线绑定点
    //     VkPipeline                                  pipeline             // 管线
    // ); // 绑定管线
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, *g_GraphicsPipeline); // 绑定管线

    // void vkCmdDraw(
    //     VkCommandBuffer                             commandBuffer,       // 命令缓冲区
    //     uint32_t                                    vertexCount,         // 顶点数量
    //     uint32_t                                    instanceCount,       // 实例数量
    //     uint32_t                                    firstVertex,         // 第一个顶点
    //     uint32_t                                    firstInstance        // 第一个实例
    // ); // 绘制三角形
    vkCmdDraw(commandBuffer, 3, 1, 0, 0); // 绘制三角形

    // void vkCmdEndRenderPass(
    //     VkCommandBuffer                             commandBuffer        // 命令缓冲区
    // ); // 结束渲染通道
    vkCmdEndRenderPass(commandBuffer); // 结束渲染通道

    if(vkEndCommandBuffer(commandBuffer) != VK_SUCCESS){ // 结束命令缓冲区
        throw std::runtime_error("Failed to record command buffer!"); // 失败
    }
}

void framebufferResizeCallback(GLFWwindow* window, int width, int height){ // 窗口大小改变回调
    g_FramebufferResized = true; // 帧缓冲区大小改变
}

void recreateSwapChain(){ // 重新创建交换链
    int width = 0, height = 0; // 窗口宽度和高度
    glfwGetFramebufferSize(g_Window, &width, &height); // 获取窗口宽度和高度

    while(width == 0 || height == 0){ // 如果窗口宽度或高度为 0
        glfwGetFramebufferSize(g_Window, &width, &height); // 获取窗口宽度和高度
        glfwWaitEvents(); // 等待事件
    }

    vkDeviceWaitIdle(*g_Device); // 等待设备空闲

    cleanupSwapChain(); // 清理交换链

    createRenderPass(); // 创建渲染通道
    createSwapChain(); // 创建交换链
    createGraphicsPipeline(); // 创建图形管线

    std::cout << "Swapchain recreated\n";
}
void cleanupSwapChain(){ // 清理交换链
    g_GraphicsPipeline.reset(); // unique_ptr 析构 GraphicsPipeline 并销毁 VkPipeline 和 VkPipelineLayout
    g_SwapChain.reset(); // unique_ptr 析构 SwapChain 并销毁 framebuffers/imageViews/swapchain
    g_RenderPass.reset(); // unique_ptr 析构 RenderPass 并销毁 VkRenderPass
}