#include "vulkan/Instance.h"
#include "vulkan/Surface.h"
#include "vulkan/PhysicalDevice.h"
#include "vulkan/Device.h"
#include "vulkan/ImageView.h"
#include "vulkan/RenderPass.h"
#include "vulkan/Pipeline.h"    


#include <vulkan/vulkan.h>
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>

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
std::unique_ptr<vkp::Instance> g_Instance;                          // Vulkan 实例
std::unique_ptr<vkp::Surface> g_Surface;                            // 窗口表面，用于呈现图像到窗口
std::unique_ptr<vkp::PhysicalDevice> g_PhysicalDevice;              // 物理设备，GPU
std::unique_ptr<vkp::Device> g_Device;                              // 逻辑设备，用于执行 Vulkan 命令
std::vector<std::unique_ptr<vkp::ImageView>> g_SwapChainImageViews; // 交换链图像视图，用于存储呈现到屏幕的图像缓冲区的视图
std::unique_ptr<vkp::RenderPass> g_RenderPass;                      // 渲染通道，用于描述渲染过程
std::unique_ptr<vkp::Pipeline> g_GraphicsPipeline;                  // 图形管线，用于描述图形渲染过程

VkSwapchainKHR g_SwapChain = VK_NULL_HANDLE;                    // 交换链，用于管理呈现到屏幕的图像缓冲区序列
std::vector<VkImage> g_SwapChainImages;                         // 交换链图像，用于存储呈现到屏幕的图像缓冲区
VkFormat g_SwapChainImageFormat;                                // 交换链图像格式，用于存储呈现到屏幕的图像缓冲区的格式
VkExtent2D g_SwapChainExtent;                                   // 交换链图像大小，用于存储呈现到屏幕的图像缓冲区的大小
std::vector<VkFramebuffer> g_SwapChainFramebuffers;             // 交换链帧缓冲区，用于存储呈现到屏幕的图像缓冲区的帧缓冲区
VkCommandPool g_CommandPool = VK_NULL_HANDLE;                   // 命令池，用于分配命令缓冲区
std::vector<VkCommandBuffer> g_CommandBuffers;                  // 命令缓冲区，用于存储 Vulkan 命令
std::vector<VkSemaphore> g_ImageAvailableSemaphores;            // 图像可用信号量，用于同步图像的可用性, GPU 等它，表示 swapchain image 已 acquire，可以开始写
std::vector<VkSemaphore> g_RenderFinishedSemaphores;            // 渲染完成信号量，用于同步图像的呈现完成, present queue 等它，表示渲染已结束，可以拿去显示
std::vector<VkFence> g_InFlightFences;                          // 并发帧信号量，用于同步并发帧的完成, CPU 等它，表示上一轮使用这个 frame slot 的 GPU 工作已完成，可以安全重录 command buffer

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
VkSurfaceFormatKHR chooseSwapSurfaceFormat(const std::vector<VkSurfaceFormatKHR>& availableFormats); // 选择交换链图像格式
VkPresentModeKHR chooseSwapPresentMode(const std::vector<VkPresentModeKHR>& availablePresentModes); // 选择交换链呈现模式
VkExtent2D chooseSwapExtent(const VkSurfaceCapabilitiesKHR& capabilities); // 选择交换链图像大小

void createImageViews();        // 创建图像视图
void createRenderPass();        // 创建渲染通道
void createGraphicsPipeline();  // 创建图形管线

void createFramebuffers();      // 创建帧缓冲区
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

    createSwapChain();          // 创建交换链
    createImageViews();         // 创建图像视图
    createRenderPass();         // 创建渲染通道
    createGraphicsPipeline();   // 创建图形管线
    createFramebuffers();       // 创建帧缓冲区
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
    if(*g_Device != VK_NULL_HANDLE){ // 确保没有 GPU 命令还在用 framebuffer/pipeline/swapchain
        vkDeviceWaitIdle(*g_Device); // 等待设备空闲
    }

    cleanupSwapChain(); // 清理交换链

    for(size_t i = 0; i < g_ImageAvailableSemaphores.size(); i++){ // 销毁图像可用信号量
        if(g_ImageAvailableSemaphores[i] != VK_NULL_HANDLE){
            vkDestroySemaphore(*g_Device, g_ImageAvailableSemaphores[i], nullptr);
        }
    }
    for(size_t i = 0; i < g_RenderFinishedSemaphores.size(); i++){ // 销毁渲染完成信号量
        if(g_RenderFinishedSemaphores[i] != VK_NULL_HANDLE){
            vkDestroySemaphore(*g_Device, g_RenderFinishedSemaphores[i], nullptr);
        }
    }
    for(size_t i = 0; i < g_InFlightFences.size(); i++){ // 销毁并发帧信号量
        if(g_InFlightFences[i] != VK_NULL_HANDLE){
            vkDestroyFence(*g_Device, g_InFlightFences[i], nullptr);
        }
    }

    if(g_CommandPool != VK_NULL_HANDLE){ // 销毁命令池
        vkDestroyCommandPool(*g_Device, g_CommandPool, nullptr);
    } // 不需要手动 vkFreeCommandBuffers，销毁 command pool 会释放其 command buffers


    g_Device.reset();           // 销毁逻辑设备, Device 必须早于 Surface/Instance 销毁
    g_PhysicalDevice.reset();   // PhysicalDevice 不拥有 Vulkan 资源，不用销毁。但为顺序清晰，在 device 销毁后 reset
    g_Surface.reset();          // 销毁窗口表面
    g_Instance.reset();         // std::unique_ptr 的 reset() 会先释放其所有权，即调用当前所指向的 vkp::Instance 的析构函数

    if(g_Window != nullptr){
        glfwDestroyWindow(g_Window); // 销毁窗口
    }
    glfwTerminate(); // 终止 GLFW
}


void createSwapChain(){
    vkp::SwapChainSupportDetails swapChainSupport = g_PhysicalDevice->QuerySwapChainSupport(); // 查询交换链支持信息

    VkSurfaceFormatKHR surfaceFormat = chooseSwapSurfaceFormat(swapChainSupport.formats); // 选择交换链图像格式
    VkPresentModeKHR presentMode = chooseSwapPresentMode(swapChainSupport.presentModes); // 选择交换链呈现模式
    VkExtent2D extent = chooseSwapExtent(swapChainSupport.capabilities); // 选择交换链图像大小

    uint32_t imageCount = swapChainSupport.capabilities.minImageCount + 1; // 图像数量, 至少为 minImageCount + 1

    if(swapChainSupport.capabilities.maxImageCount > 0 && 
        imageCount > swapChainSupport.capabilities.maxImageCount){ 
        // 如果最大图像数量不为0且图像数量大于最大图像数量
        imageCount = swapChainSupport.capabilities.maxImageCount; // 图像数量为最大图像数量
    }

    VkSwapchainCreateInfoKHR createInfo{}; // 交换链创建信息
    createInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR; // 结构体类型
    createInfo.surface = *g_Surface; // 表面
    createInfo.minImageCount = imageCount; // 图像数量
    createInfo.imageFormat = surfaceFormat.format; // 图像格式
    createInfo.imageColorSpace = surfaceFormat.colorSpace; // 图像颜色空间
    createInfo.imageExtent = extent; // 图像大小
    createInfo.imageArrayLayers = 1; // 图像数组层数
    createInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT; // 图像使用方式

    vkp::QueueFamilyIndices indices = g_PhysicalDevice->GetQueueFamilyIndices(); // 查找队列族索引
    uint32_t queueFamilyIndices[] = {
        indices.graphicsFamily.value(), 
        indices.presentFamily.value()
    }; // 队列族索引数组

    if(indices.graphicsFamily != indices.presentFamily){ // 如果图形队列族索引不等于呈现队列族索引, 表示支持并发
        // 并发模式 VK_SHARING_MODE_CONCURRENT
        // 条件：图形队列族 ≠ 呈现队列族（例如某些集成显卡或特殊配置）
        // 含义：图像可以同时被多个指定的队列族访问，无需显式所有权转移
        // 代价：由于多族可能同时读写同一资源，驱动必须使用更保守的内存管理策略，性能略低，但避免了手动转移所有权的复杂性
        createInfo.imageSharingMode = VK_SHARING_MODE_CONCURRENT; // 图像共享模式为并发
        createInfo.queueFamilyIndexCount = 2; // 队列族索引数量为2
        createInfo.pQueueFamilyIndices = queueFamilyIndices; // 队列族索引数组    
    }
    else{
        // 独占模式 VK_SHARING_MODE_EXCLUSIVE 同族独占模式正是为了避免所有权转移的麻烦，同时获得最佳性能
        // 族（Queue Family，队列族） 是 GPU 上按功能划分的一组命令队列
        // 条件：图形队列族 == 呈现队列族（比如都从族索引 0 创建，RTX 5070 就是这种情况, 族 0 既能画图又能呈现）
        // 含义：图像在任意时刻只能被一个队列族访问（所有权明确）
        // 所有权转移：如果将来需要跨族使用，必须通过显式的所有权转移操作（VkImageMemoryBarrier 配合 vkQueueFamilyOwnershipTransfer）。但在同一族内，天然就独占，无需额外操作
        // 性能优势：驱动可以利用独占特性做更激进的硬件优化（如缓存策略），因为不用考虑多族并发访问，所以更高效
        createInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE; // 图像共享模式为独占, 表示不支持并发
        createInfo.queueFamilyIndexCount = 0; // 队列族索引数量为0
        createInfo.pQueueFamilyIndices = nullptr; // 队列族索引数组
    }

    createInfo.preTransform = swapChainSupport.capabilities.currentTransform; // 图像转换方式
    createInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR; // 图像透明度
    createInfo.presentMode = presentMode; // 呈现模式
    createInfo.clipped = VK_TRUE; // 裁剪
    createInfo.oldSwapchain = VK_NULL_HANDLE; // 旧交换链

    if(vkCreateSwapchainKHR(*g_Device, &createInfo, nullptr, &g_SwapChain) != VK_SUCCESS){ // 创建交换链
        throw std::runtime_error("Failed to create swap chain!"); // 失败
    }

    vkGetSwapchainImagesKHR(*g_Device, g_SwapChain, &imageCount, nullptr); // 获取交换链图像数量
    g_SwapChainImages.resize(imageCount); // 调整交换链图像大小
    vkGetSwapchainImagesKHR(*g_Device, g_SwapChain, &imageCount, g_SwapChainImages.data()); // 获取交换链图像

    g_SwapChainImageFormat = surfaceFormat.format; // 交换链图像格式
    g_SwapChainExtent = extent; // 交换链图像大小

    std::cout << "Swapchain image count: " << g_SwapChainImages.size() << "\n";
    std::cout << "Swapchain format: " << g_SwapChainImageFormat << "\n";
    std::cout << "Swapchain extent: " << g_SwapChainExtent.width << " x " << g_SwapChainExtent.height << "\n";
    std::cout << "Swapchain: OK\n";
}
VkSurfaceFormatKHR chooseSwapSurfaceFormat(const std::vector<VkSurfaceFormatKHR>& availableFormats){ // 选择交换链图像格式
    for(const auto& availableFormat:availableFormats){ // 遍历可用的图像格式
        if(availableFormat.format == VK_FORMAT_B8G8R8A8_SRGB && 
            availableFormat.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR){ 
                // 如果图像格式为 B8G8R8A8_SRGB 且颜色空间为 SRGB_NONLINEAR_KHR, 最通用、适合显示器的格式
            return availableFormat; // 返回图像格式
        }    
    }
    return availableFormats[0]; // 返回第一个图像格式
}
VkPresentModeKHR chooseSwapPresentMode(const std::vector<VkPresentModeKHR>& availablePresentModes){ // 选择交换链呈现模式
    for(const auto& availablePresentMode:availablePresentModes){ // 遍历可用的呈现模式
        // MAILBOX 模式（优先选择）
        // 行为：维护一个图像队列，当队列满时，新提交的图像会直接替换队尾等待显示的图像，而不是阻塞。
        // 效果：延迟极低，不会产生画面撕裂，且能避免旧帧堆积导致的卡顿。常用于追求低输入延迟的实时渲染（如游戏）。
        // 缓冲数：通常需要至少 3 个交换链图像（否则队列无法“丢弃”旧帧），因此常被称作三重缓冲的一种实现，但并不是经典的三重缓冲排队，而是直接丢弃未显示的旧帧
        if(availablePresentMode == VK_PRESENT_MODE_MAILBOX_KHR){ // 如果呈现模式为 MAILBOX_KHR, 低延迟三缓冲，适合渲染
            return availablePresentMode; // 返回呈现模式
        }
    }
    // FIFO 模式（回退选择）
    // 行为：类似传统的垂直同步，按队列顺序依次显示图像。如果队列满了，提交必须等待，直到有空位。
    // 效果：画面无撕裂，但可能导致明显的输入延迟和帧率锁定到刷新率。缓冲数可以为 2（双缓冲）或更多。
    // FIFO 是 Vulkan 规范唯一强制必须支持的模式，所以作为安全的回退选项。
    return VK_PRESENT_MODE_FIFO_KHR; // 返回 FIFO_KHR, 垂直同步（可看作双缓冲或更多）   
}
VkExtent2D chooseSwapExtent(const VkSurfaceCapabilitiesKHR& capabilities){ // 选择交换链图像大小
    if(capabilities.currentExtent.width != std::numeric_limits<uint32_t>::max()){ // 如果当前图像大小不为最大值
        // 表示当前表面有一个确定的、非变化的大小（比如窗口精确匹配该尺寸）
        // 这种情况常见于某些窗口管理器，它们强制交换链图像大小等于窗口大小
        // 此时直接返回 currentExtent，应用不能自行修改
        return capabilities.currentExtent; // 返回当前图像大小
    }
    
    int width = 0, height = 0; // 窗口宽度和高度
    glfwGetFramebufferSize(g_Window, &width, &height); // 获取窗口宽度和高度

    VkExtent2D actualExtent = {
        static_cast<uint32_t>(width), // 窗口宽度
        static_cast<uint32_t>(height) // 窗口高度
    };

    actualExtent.width = std::clamp(
        actualExtent.width, // 实际宽度
        capabilities.minImageExtent.width, // 最小图像宽度
        capabilities.maxImageExtent.width // 最大图像宽度
    );

    actualExtent.height = std::clamp(
        actualExtent.height, // 实际高度
        capabilities.minImageExtent.height, // 最小图像高度
        capabilities.maxImageExtent.height // 最大图像高度
    );

    return actualExtent; // 返回实际图像大小
}

void createImageViews(){
    g_SwapChainImageViews.clear(); // 清空交换链图像视图
    g_SwapChainImageViews.reserve(g_SwapChainImages.size()); // 调整交换链图像视图大小
    // resize(n)：真的创建 n 个元素； reserve(n)：只预留容量，不创建元素

    for(size_t i = 0; i < g_SwapChainImages.size(); i++){ // 遍历交换链图像
        g_SwapChainImageViews.push_back(
            std::make_unique<vkp::ImageView>(*g_Device, g_SwapChainImages[i], g_SwapChainImageFormat)
        ); // 创建交换链图像视图
    }

    std::cout << "Created image views: " << g_SwapChainImageViews.size() << "\n";
    std::cout << "Image views: OK\n"; // 成功
}

void createRenderPass(){
    g_RenderPass = std::make_unique<vkp::RenderPass>(*g_Device, g_SwapChainImageFormat); // 创建渲染通道
}
void createGraphicsPipeline(){
    g_GraphicsPipeline = std::make_unique<vkp::Pipeline>(
        *g_Device, 
        *g_RenderPass,
        "shaders/stage1_triangle.vert.spv",
        "shaders/stage1_triangle.frag.spv"
    ); // 创建图形管线
}

void createFramebuffers(){ // 创建帧缓冲区
    g_SwapChainFramebuffers.resize(g_SwapChainImageViews.size()); // 帧缓冲区大小

    for(size_t i = 0; i < g_SwapChainImageViews.size(); i++){ // 遍历交换链图像视图
        VkImageView attachments[] = { // 帧缓冲区附件
            *g_SwapChainImageViews[i] // 交换链图像视图
        };
        
        VkFramebufferCreateInfo framebufferInfo{}; // 帧缓冲区创建信息
        framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO; // 结构体类型
        framebufferInfo.renderPass = *g_RenderPass; // 渲染通道
        framebufferInfo.attachmentCount = 1; // 附件数量
        framebufferInfo.pAttachments = attachments; // 附件数组
        framebufferInfo.width = g_SwapChainExtent.width; // 帧缓冲区宽度
        framebufferInfo.height = g_SwapChainExtent.height; // 帧缓冲区高度
        framebufferInfo.layers = 1; // 层数量

        if(vkCreateFramebuffer(*g_Device, &framebufferInfo, nullptr, &g_SwapChainFramebuffers[i]) != VK_SUCCESS){ // 创建帧缓冲区
            throw std::runtime_error("Failed to create framebuffer!"); // 失败
        }
    }

    std::cout << "Created framebuffers: " << g_SwapChainFramebuffers.size() << "\n"; // 成功
}
void createCommandPool(){ // 创建命令池
    vkp::QueueFamilyIndices queueFamilyIndices = g_PhysicalDevice->GetQueueFamilyIndices(); // 队列族索引

    VkCommandPoolCreateInfo poolInfo{}; // 命令池创建信息
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO; // 结构体类型
    poolInfo.queueFamilyIndex = g_Device->GetGraphicsQueueFamily(); // 队列族索引
    poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT; // 命令缓冲区重置标志

    if(vkCreateCommandPool(*g_Device, &poolInfo, nullptr, &g_CommandPool) != VK_SUCCESS){ // 创建命令池
        throw std::runtime_error("Failed to create command pool!"); // 失败
    }

    std::cout << "Created command pool: OK\n"; // 成功
}
void createCommandBuffers(){ // 创建命令缓冲区
    g_CommandBuffers.resize(MAX_FRAMES_IN_FLIGHT); // 命令缓冲区大小

    VkCommandBufferAllocateInfo allocInfo{}; // 命令缓冲区分配信息
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO; // 结构体类型
    allocInfo.commandPool = g_CommandPool; // 命令池
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY; // 命令缓冲区级别
    allocInfo.commandBufferCount = static_cast<uint32_t>(g_CommandBuffers.size()); // 命令缓冲区数量

    if(vkAllocateCommandBuffers(*g_Device, &allocInfo, g_CommandBuffers.data()) != VK_SUCCESS){ // 分配命令缓冲区
        throw std::runtime_error("Failed to allocate command buffers!"); // 失败
    }

    std::cout << "Command buffers: " << g_CommandBuffers.size() << "\n";
}
void createSyncObjects(){ // 创建同步对象
    g_ImageAvailableSemaphores.resize(MAX_FRAMES_IN_FLIGHT); // 图像可用信号量大小
    g_RenderFinishedSemaphores.resize(MAX_FRAMES_IN_FLIGHT); // 渲染完成信号量大小
    g_InFlightFences.resize(MAX_FRAMES_IN_FLIGHT); // 并发帧信号量大小

    VkSemaphoreCreateInfo semaphoreInfo{}; // 信号量创建信息
    semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO; // 结构体类型

    VkFenceCreateInfo fenceInfo{}; // 栅栏创建信息
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO; // 结构体类型
    fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT; // 栅栏标志

    for(size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++){ // 遍历并发帧信号量
        if(vkCreateSemaphore(*g_Device, &semaphoreInfo, nullptr, &g_ImageAvailableSemaphores[i]) != VK_SUCCESS || // 创建图像可用信号量
           vkCreateSemaphore(*g_Device, &semaphoreInfo, nullptr, &g_RenderFinishedSemaphores[i]) != VK_SUCCESS || // 创建渲染完成信号量
           vkCreateFence(*g_Device, &fenceInfo, nullptr, &g_InFlightFences[i]) != VK_SUCCESS){ // 创建并发帧信号量
            throw std::runtime_error("Failed to create synchronization objects for a frame!"); // 失败
        }    
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

    vkWaitForFences(*g_Device, 1, &g_InFlightFences[g_CurrentFrame], VK_TRUE, UINT64_MAX); // 等待栅栏

    uint32_t imageIndex = 0; // 图像索引
    // VkResult vkAcquireNextImageKHR(
    //     VkDevice                                    device,      // 设备
    //     VkSwapchainKHR                              swapchain,   // 交换链
    //     uint64_t                                    timeout,     // 超时时间
    //     VkSemaphore                                 semaphore,   // 信号量
    //     VkFence                                     fence,       // 栅栏
    //     uint32_t*                                   pImageIndex  // 图像索引
    // ); // 获取下一个图像
    VkResult result = vkAcquireNextImageKHR(*g_Device, g_SwapChain, UINT64_MAX, g_ImageAvailableSemaphores[g_CurrentFrame], VK_NULL_HANDLE, &imageIndex); // 等待图像可用

    if(result == VK_ERROR_OUT_OF_DATE_KHR){ // 交换链已过期
        recreateSwapChain(); // 重新创建交换链
        return;    
    }
    else if(result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR){ // 其他错误
        throw std::runtime_error("Failed to acquire swap chain image!"); // 失败
    }

    vkResetFences(*g_Device, 1, &g_InFlightFences[g_CurrentFrame]); // 重置栅栏, 防止上一帧的栅栏未完成导致当前帧无法开始

    vkResetCommandBuffer(g_CommandBuffers[g_CurrentFrame], 0); // 重置命令缓冲区
    recordCommandBuffer(g_CommandBuffers[g_CurrentFrame], imageIndex); // 记录命令缓冲区



    VkSemaphore waitSemaphores[] = {g_ImageAvailableSemaphores[g_CurrentFrame]}; // 等待信号量
    VkPipelineStageFlags waitStages[] = {VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT}; // 等待阶段
    VkSemaphore signalSemaphores[] = {g_RenderFinishedSemaphores[g_CurrentFrame]}; // 信号量

    VkSubmitInfo submitInfo{}; // 提交信息
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO; // 结构体类型
    submitInfo.waitSemaphoreCount = 1; // 等待信号量数量
    submitInfo.pWaitSemaphores = waitSemaphores; // 等待信号量数组
    submitInfo.pWaitDstStageMask = waitStages; // 等待阶段数组
    submitInfo.commandBufferCount = 1; // 命令缓冲区数量
    submitInfo.pCommandBuffers = &g_CommandBuffers[g_CurrentFrame]; // 命令缓冲区数组
    submitInfo.signalSemaphoreCount = 1; // 信号量数量
    submitInfo.pSignalSemaphores = signalSemaphores; // 信号量数组

    // VkResult vkQueueSubmit(
    //     VkQueue                                     queue,       // 队列
    //     uint32_t                                    submitCount, // 提交数量
    //     const VkSubmitInfo*                         pSubmits,    // 提交信息数组
    //     VkFence                                     fence        // 栅栏
    // ); // 提交命令缓冲区
    if(vkQueueSubmit(g_Device->GetGraphicsQueue(), 1, &submitInfo, g_InFlightFences[g_CurrentFrame]) != VK_SUCCESS){ // 提交命令缓冲区
        throw std::runtime_error("Failed to submit draw command buffer!"); // 失败
    }


    
    VkSwapchainKHR swapChains[] = {g_SwapChain}; // 交换链数组

    VkPresentInfoKHR presentInfo{}; // 呈现信息
    presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR; // 结构体类型
    presentInfo.waitSemaphoreCount = 1; // 等待信号量数量
    presentInfo.pWaitSemaphores = signalSemaphores; // 等待信号量数组
    presentInfo.swapchainCount = 1; // 交换链数量
    presentInfo.pSwapchains = swapChains; // 交换链数组
    presentInfo.pImageIndices = &imageIndex; // 图像索引数组

    // VkResult vkQueuePresentKHR(
    //     VkQueue                                     queue,       // 队列
    //     const VkPresentInfoKHR*                     pPresentInfo // 呈现信息
    // );
    result = vkQueuePresentKHR(g_Device->GetPresentQueue(), &presentInfo); // 呈现图像

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
    VkCommandBufferBeginInfo beginInfo{}; // 命令缓冲区开始信息
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO; // 结构体类型

    if(vkBeginCommandBuffer(commandBuffer, &beginInfo) != VK_SUCCESS){ // 开始命令缓冲区
        throw std::runtime_error("Failed to begin recording command buffer!"); // 失败
    }

    VkRenderPassBeginInfo renderPassInfo{}; // 渲染通道开始信息
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO; // 结构体类型
    renderPassInfo.renderPass = *g_RenderPass; // 渲染通道
    renderPassInfo.framebuffer = g_SwapChainFramebuffers[imageIndex]; // 帧缓冲区
    renderPassInfo.renderArea.offset = {0, 0}; // 渲染区域偏移
    renderPassInfo.renderArea.extent = g_SwapChainExtent; // 渲染区域大小

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
    viewport.width = static_cast<float>(g_SwapChainExtent.width); // 视口宽度
    viewport.height = static_cast<float>(g_SwapChainExtent.height); // 视口高度
    viewport.minDepth = 0.0f; // 视口最小深度
    viewport.maxDepth = 1.0f; // 视口最大深度
    vkCmdSetViewport(commandBuffer, 0, 1, &viewport); // 设置视口

    VkRect2D scissor{}; // 裁剪矩形, 负责像素丢弃
    scissor.offset = {0, 0}; // 视口偏移
    scissor.extent = g_SwapChainExtent; // 视口大小
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

    createSwapChain(); // 创建交换链
    createImageViews(); // 创建图像视图
    createRenderPass(); // 创建渲染通道
    createGraphicsPipeline(); // 创建图形管线
    createFramebuffers(); // 创建帧缓冲区

    std::cout << "Swapchain recreated\n";
}
void cleanupSwapChain(){ // 清理交换链
    for(auto framebuffer : g_SwapChainFramebuffers){ // 销毁帧缓冲区
        vkDestroyFramebuffer(*g_Device, framebuffer, nullptr); // 销毁帧缓冲区    
    }
    g_SwapChainFramebuffers.clear(); // 清空帧缓冲区数组

    g_GraphicsPipeline.reset(); // unique_ptr 析构 GraphicsPipeline 并销毁 VkPipeline 和 VkPipelineLayout
    g_RenderPass.reset(); // unique_ptr 析构 RenderPass 并销毁 VkRenderPass
    g_SwapChainImageViews.clear(); // 清空图像视图数组，unique_ptr 析构 ImageView 并销毁 VkImageView

    if(g_SwapChain != VK_NULL_HANDLE){ // 销毁交换链
        vkDestroySwapchainKHR(*g_Device, g_SwapChain, nullptr); // 销毁交换链
        g_SwapChain = VK_NULL_HANDLE; // 置空交换链
    }
}