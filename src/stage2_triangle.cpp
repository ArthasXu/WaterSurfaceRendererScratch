#include "vulkan/Instance.h"
#include "vulkan/Surface.h"

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
std::unique_ptr<vkp::Instance> g_Instance;                      // Vulkan 实例
std::unique_ptr<vkp::Surface> g_Surface;                        // 窗口表面，用于呈现图像到窗口

VkPhysicalDevice g_PhysicalDevice = VK_NULL_HANDLE;             // 物理设备，即 GPU
VkDevice g_Device = VK_NULL_HANDLE;                             // 逻辑设备，用于执行 Vulkan 命令
VkQueue g_GraphicsQueue = VK_NULL_HANDLE;                       // 图形队列，用于执行图形命令
VkQueue g_PresentQueue = VK_NULL_HANDLE;                        // 呈现队列，用于呈现图像到窗口
VkSwapchainKHR g_SwapChain = VK_NULL_HANDLE;                    // 交换链，用于管理呈现到屏幕的图像缓冲区序列
std::vector<VkImage> g_SwapChainImages;                         // 交换链图像，用于存储呈现到屏幕的图像缓冲区
VkFormat g_SwapChainImageFormat;                                // 交换链图像格式，用于存储呈现到屏幕的图像缓冲区的格式
VkExtent2D g_SwapChainExtent;                                   // 交换链图像大小，用于存储呈现到屏幕的图像缓冲区的大小
std::vector<VkImageView> g_SwapChainImageViews;                 // 交换链图像视图，用于存储呈现到屏幕的图像缓冲区的视图
VkRenderPass g_RenderPass = VK_NULL_HANDLE;                     // 渲染通道，用于描述渲染过程
VkPipelineLayout g_PipelineLayout = VK_NULL_HANDLE;             // 管线布局，用于描述管线的输入和输出
VkPipeline g_GraphicsPipeline = VK_NULL_HANDLE;                 // 图形管线，用于描述图形渲染过程
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
const std::vector<const char*> g_DeviceExtensions = {
    VK_KHR_SWAPCHAIN_EXTENSION_NAME // VK_KHR_SWAPCHAIN_EXTENSION_NAME 是一个宏，定义为 "VK_KHR_swapchain"
};

struct QueueFamilyIndices{ // 用于存储队列族索引
    // Vulkan 将各种操作（图形、计算、传输、呈现）分配到不同的队列族上执行。每个 GPU 的队列族数量、能力都不同
    std::optional<uint32_t> graphicsFamily; // std::optional 是一个模板类，用于表示可能存在或不存在的值
    std::optional<uint32_t> presentFamily;
    bool isComplete() const {
        return graphicsFamily.has_value() && presentFamily.has_value(); // has_value() 方法用于检查 std::optional 对象是否包含值
    }
};

struct SwapChainSupportDetails{ // 用于存储交换链支持信息, 负责管理呈现到屏幕的图像缓冲区序列 
    VkSurfaceCapabilitiesKHR capabilities;      // 交换链的能力, 如最小和最大图像数量、支持的图像格式和大小等
    std::vector<VkSurfaceFormatKHR> formats;    // 支持的图像格式, 如颜色深度和通道数
    std::vector<VkPresentModeKHR> presentModes; // 支持的呈现模式, 如立即呈现、双缓冲等
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

void pickPhysicalDevice();      // 选择物理设备
bool isDeviceSuitable(VkPhysicalDevice device); // 判断设备是否适合
QueueFamilyIndices findQueueFamilies(VkPhysicalDevice device); // 查找队列族索引
bool checkDeviceExtensionSupport(VkPhysicalDevice device); // 检查设备扩展支持
SwapChainSupportDetails querySwapChainSupport(VkPhysicalDevice device); // 查询交换链支持信息
int rateDevice(VkPhysicalDevice device); // 评分设备

void createLogicalDevice();     // 创建逻辑设备

void createSwapChain();         // 创建交换链
VkSurfaceFormatKHR chooseSwapSurfaceFormat(const std::vector<VkSurfaceFormatKHR>& availableFormats); // 选择交换链图像格式
VkPresentModeKHR chooseSwapPresentMode(const std::vector<VkPresentModeKHR>& availablePresentModes); // 选择交换链呈现模式
VkExtent2D chooseSwapExtent(const VkSurfaceCapabilitiesKHR& capabilities); // 选择交换链图像大小

void createImageViews();        // 创建图像视图
void createRenderPass();        // 创建渲染通道
void createGraphicsPipeline();  // 创建图形管线
std::vector<char> readFile(const std::string& filename); // 读取文件
VkShaderModule createShaderModule(const std::vector<char>& code); // 创建着色器模块

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
    g_Instance = std::make_unique<vkp::Instance>(g_EnableValidationLayers); // 创建实例
    g_Surface = std::make_unique<vkp::Surface>(*g_Instance, g_Window);      // 创建窗口表面

    pickPhysicalDevice();       // 选择物理设备
    createLogicalDevice();      // 创建逻辑设备
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

    vkDeviceWaitIdle(g_Device); // 等待设备空闲
}
void cleanup(){  // 清理资源
    if(g_Device != VK_NULL_HANDLE){ // 确保没有 GPU 命令还在用 framebuffer/pipeline/swapchain
        vkDeviceWaitIdle(g_Device); // 等待设备空闲
    }

    // for(auto framebuffer : g_SwapChainFramebuffers){ // 销毁帧缓冲区, framebuffer 引用 render pass 和 image view
    //     vkDestroyFramebuffer(g_Device, framebuffer, nullptr);
    // }
    // if(g_GraphicsPipeline != VK_NULL_HANDLE){
    //     vkDestroyPipeline(g_Device, g_GraphicsPipeline, nullptr); // 销毁图形管线
    // }
    // if(g_PipelineLayout != VK_NULL_HANDLE){
    //     vkDestroyPipelineLayout(g_Device, g_PipelineLayout, nullptr); // 销毁管线布局
    // }
    // if(g_RenderPass != VK_NULL_HANDLE){
    //     vkDestroyRenderPass(g_Device, g_RenderPass, nullptr); // 销毁渲染通道
    // }
    // for(auto imageView : g_SwapChainImageViews){ // 销毁图像视图
    //     vkDestroyImageView(g_Device, imageView, nullptr);
    // }
    // if(g_SwapChain != VK_NULL_HANDLE){
    //     vkDestroySwapchainKHR(g_Device, g_SwapChain, nullptr); // 销毁交换链
    // }
    cleanupSwapChain(); // 清理交换链

    for(size_t i = 0; i < g_ImageAvailableSemaphores.size(); i++){ // 销毁图像可用信号量
        if(g_ImageAvailableSemaphores[i] != VK_NULL_HANDLE){
            vkDestroySemaphore(g_Device, g_ImageAvailableSemaphores[i], nullptr);
        }
    }
    for(size_t i = 0; i < g_RenderFinishedSemaphores.size(); i++){ // 销毁渲染完成信号量
        if(g_RenderFinishedSemaphores[i] != VK_NULL_HANDLE){
            vkDestroySemaphore(g_Device, g_RenderFinishedSemaphores[i], nullptr);
        }
    }
    for(size_t i = 0; i < g_InFlightFences.size(); i++){ // 销毁并发帧信号量
        if(g_InFlightFences[i] != VK_NULL_HANDLE){
            vkDestroyFence(g_Device, g_InFlightFences[i], nullptr);
        }
    }

    if(g_CommandPool != VK_NULL_HANDLE){ // 销毁命令池
        vkDestroyCommandPool(g_Device, g_CommandPool, nullptr);
    } // 不需要手动 vkFreeCommandBuffers，销毁 command pool 会释放其 command buffers

    if(g_Device != VK_NULL_HANDLE){
        vkDestroyDevice(g_Device, nullptr); // 销毁逻辑设备, Device 必须早于 Surface/Instance 销毁
    }

    g_Surface.reset();  // 销毁窗口表面
    g_Instance.reset(); // std::unique_ptr 的 reset() 会先释放其所有权，即调用当前所指向的 vkp::Instance 的析构函数

    if(g_Window != nullptr){
        glfwDestroyWindow(g_Window); // 销毁窗口
    }
    glfwTerminate(); // 终止 GLFW
}

void pickPhysicalDevice(){ // 选择物理设备
    uint32_t deviceCount = 0; // 设备数量
    vkEnumeratePhysicalDevices(*g_Instance, &deviceCount, nullptr); // 获取设备数量

    if(deviceCount == 0){ // 如果设备数量为 0
        throw std::runtime_error("Failed to find GPUs with Vulkan support!"); // 抛出异常
    }

    std::vector<VkPhysicalDevice> devices(deviceCount); // 设备数组
    vkEnumeratePhysicalDevices(*g_Instance, &deviceCount, devices.data()); // 获取设备数组
    std::cout<<"Physical devices: "<<deviceCount<<"\n";

    VkPhysicalDevice bestDevice = VK_NULL_HANDLE; // 最佳设备句柄
    int bestScore = 0; // 最佳评分

    for(const auto& device:devices){ // 遍历设备
        VkPhysicalDeviceProperties deviceProperties; // 设备属性
        vkGetPhysicalDeviceProperties(device, &deviceProperties); // 获取设备属性
        
        std::cout<<"  "<<deviceProperties.deviceName<<"\n"; // 输出设备名称

        int score = rateDevice(device); // 评分
        if(score > bestScore){ // 如果评分大于最佳评分
            bestScore = score; // 更新最佳评分
            bestDevice = device; // 更新物理设备句柄
        }
    }

    if(bestDevice == VK_NULL_HANDLE){ // 如果最佳设备句柄为空
        throw std::runtime_error("Failed to find a suitable GPU!"); // 抛出异常
    }

    g_PhysicalDevice = bestDevice; // 更新物理设备句柄

    VkPhysicalDeviceProperties selectedProperties{}; // 设备属性
    vkGetPhysicalDeviceProperties(g_PhysicalDevice, &selectedProperties); // 获取设备属性

    QueueFamilyIndices indices = findQueueFamilies(g_PhysicalDevice); // 查找队列族索引

    std::cout << "\nSelected GPU:\n";
    std::cout << "  " << selectedProperties.deviceName << "\n\n";
    std::cout << "Graphics queue family: " << indices.graphicsFamily.value() << "\n";
    std::cout << "Present queue family: " << indices.presentFamily.value() << "\n";
    std::cout << "Swapchain support: OK\n";
}
QueueFamilyIndices findQueueFamilies(VkPhysicalDevice device){ // 为指定的物理设备查找所需的队列族索引（图形、呈现），并返回一个包含索引的结构体
    QueueFamilyIndices indices; // 队列族索引
    uint32_t queueFamilyCount = 0; // 队列族数量
    vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, nullptr); // 获取队列族数量
    std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount); // 队列族属性数组
    vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, queueFamilies.data()); // 获取队列族属性

    for(uint32_t i = 0; i < queueFamilyCount; ++i){ // 遍历队列族
        if(queueFamilies[i].queueFlags & VK_QUEUE_GRAPHICS_BIT){ // 如果队列族支持图形
            indices.graphicsFamily = i; // 记录图形队列族索引
        }
        VkBool32 presentSupport = false; // 记录是否支持呈现
        vkGetPhysicalDeviceSurfaceSupportKHR(device, i, *g_Surface, &presentSupport); // 获取是否支持呈现
        if(presentSupport){ // 如果支持呈现
            indices.presentFamily = i; // 记录呈现队列族索引
        }
        if(indices.isComplete()){ // 如果队列族索引完整
            break;
        }
    }

    return indices; // 返回队列族索引
}
bool checkDeviceExtensionSupport(VkPhysicalDevice device){ // 检查设备扩展支持
    uint32_t extensionCount = 0; // 扩展数量
    vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount, nullptr); // 获取扩展数量
    std::vector<VkExtensionProperties> availableExtensions(extensionCount); // 扩展属性数组
    vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount, availableExtensions.data()); // 获取扩展属性

    std::set<std::string> requiredExtensions(g_DeviceExtensions.begin(), g_DeviceExtensions.end()); // 所需扩展集合
    for(const auto& extension:availableExtensions){ // 遍历扩展
        requiredExtensions.erase(extension.extensionName); // 从所需扩展集合中删除已支持的扩展
    }

    return requiredExtensions.empty(); // 如果所需扩展集合为空，则表示所有扩展都已支持
}
SwapChainSupportDetails querySwapChainSupport(VkPhysicalDevice device){ // 查询交换链支持
    SwapChainSupportDetails details; // 交换链支持信息

    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(device, *g_Surface, &details.capabilities); // 获取交换链能力
    
    uint32_t formatCount = 0; // 图像格式数量
    vkGetPhysicalDeviceSurfaceFormatsKHR(device, *g_Surface, &formatCount, nullptr); // 获取图像格式数量
    if(formatCount != 0){ // 如果图像格式数量不为 0
        details.formats.resize(formatCount); // 调整图像格式数组大小    
        vkGetPhysicalDeviceSurfaceFormatsKHR(device, *g_Surface, &formatCount, details.formats.data()); // 获取图像格式数组
    }    
    
    uint32_t presentModeCount = 0; // 呈现模式数量
    vkGetPhysicalDeviceSurfacePresentModesKHR(device, *g_Surface, &presentModeCount, nullptr); // 获取呈现模式数量
    if(presentModeCount != 0){ // 如果呈现模式数量不为 0
        details.presentModes.resize(presentModeCount); // 调整呈现模式数组大小
        vkGetPhysicalDeviceSurfacePresentModesKHR(device, *g_Surface, &presentModeCount, details.presentModes.data()); // 获取呈现模式数组
    }

    return details; // 返回交换链支持信息
}
bool isDeviceSuitable(VkPhysicalDevice device){ // 检查设备是否适合
    QueueFamilyIndices indices = findQueueFamilies(device); // 查找队列族索引
    
    bool extensionSupported = checkDeviceExtensionSupport(device); // 检查设备扩展支持

    bool swapChainAdequate = false; // 记录交换链是否适合
    if(extensionSupported){ // 如果设备扩展支持
        SwapChainSupportDetails swapChainSupport = querySwapChainSupport(device); // 查询交换链支持    
        swapChainAdequate = 
            !swapChainSupport.formats.empty() && 
            !swapChainSupport.presentModes.empty(); // 如果交换链支持信息不为空，则表示交换链适合
    }

    // 如果队列族索引完整、设备扩展支持、交换链适合，则表示设备适合
    return indices.isComplete() && extensionSupported && swapChainAdequate;
}
int rateDevice(VkPhysicalDevice device){ // 评分设备, 独显优先，核显其次
    if(!isDeviceSuitable(device)){ // 如果设备不适合
        return 0; // 返回 0
    }

    VkPhysicalDeviceProperties deviceProperties{}; // 设备属性, VkPhysicalDeviceProperties 结构体包含了设备的基本信息，如设备类型、名称、供应商 ID 等
    VkPhysicalDeviceFeatures deviceFeatures{}; // 设备特性, VkPhysicalDeviceFeatures 结构体包含了设备支持的特性，如是否支持多视图渲染、是否支持几何着色器等

    vkGetPhysicalDeviceProperties(device, &deviceProperties); // 获取设备属性, vkGetPhysicalDeviceProperties 函数用于获取设备的基本信息
    vkGetPhysicalDeviceFeatures(device, &deviceFeatures); // 获取设备特性, vkGetPhysicalDeviceFeatures 函数用于获取设备支持的特性

    if(deviceProperties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU){ // 如果设备类型为离散 GPU
        return 1000; // 返回 1000    
    }
    
    if(deviceProperties.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU){ // 如果设备类型为集成 GPU
        return 100; // 返回 100
    }

    return 10; // 返回 10
}


void createLogicalDevice(){ // 创建逻辑设备 
    QueueFamilyIndices indices = findQueueFamilies(g_PhysicalDevice); // 查找队列族索引

    std::vector<VkDeviceQueueCreateInfo> queueCreateInfos; // 队列创建信息数组
    std::set<uint32_t> uniqueQueueFamilies = {
        indices.graphicsFamily.value(), 
        indices.presentFamily.value()
    }; // 唯一的队列族索引集合

    float queuePriority = 1.0f; // 队列优先级

    for(uint32_t queueFamily:uniqueQueueFamilies){ // 遍历唯一的队列族索引
        VkDeviceQueueCreateInfo queueCreateInfo{}; // 队列创建信息
        queueCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO; // 结构体类型    
        queueCreateInfo.queueFamilyIndex = queueFamily; // 队列族索引
        queueCreateInfo.queueCount = 1; // 队列数量
        queueCreateInfo.pQueuePriorities = &queuePriority; // 队列优先级数组

        queueCreateInfos.push_back(queueCreateInfo); // 添加队列创建信息
    }

    VkPhysicalDeviceFeatures deviceFeatures{}; // 设备特性

    VkDeviceCreateInfo createInfo{}; // 设备创建信息
    createInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO; // 结构体类型
    createInfo.queueCreateInfoCount = static_cast<uint32_t>(queueCreateInfos.size()); // 队列创建信息数量
    createInfo.pQueueCreateInfos = queueCreateInfos.data(); // 队列创建信息数组
    createInfo.pEnabledFeatures = &deviceFeatures; // 设备特性

    createInfo.enabledExtensionCount = static_cast<uint32_t>(g_DeviceExtensions.size()); // 启用的扩展数量
    createInfo.ppEnabledExtensionNames = g_DeviceExtensions.data(); // 启用的扩展名称数组

    // if(g_EnableValidationLayers){ // 如果启用校验层
    //     createInfo.enabledLayerCount = static_cast<uint32_t>(g_ValidationLayers.size()); // 启用的校验层数量
    //     createInfo.ppEnabledLayerNames = g_ValidationLayers.data(); // 启用的校验层名称数组
    // }
    // else{
    //     createInfo.enabledLayerCount = 0; // 启用的校验层数量为0
    // }
    createInfo.enabledLayerCount = 0; // device layer 已废弃。validation layer 只在 VkInstanceCreateInfo 开启

    // VkResult vkCreateDevice(
    //     VkPhysicalDevice                            physicalDevice,
    //     const VkDeviceCreateInfo*                   pCreateInfo,
    //     const VkAllocationCallbacks*                pAllocator,
    //     VkDevice*                                   pDevice
    // ); // 创建设备
    if(vkCreateDevice(g_PhysicalDevice, &createInfo, nullptr, &g_Device) != VK_SUCCESS){ // 创建设备
        throw std::runtime_error("Failed to create logical device!");
    }

    std::cout<<"Logical device created: OK\n";

    vkGetDeviceQueue(g_Device, indices.graphicsFamily.value(), 0, &g_GraphicsQueue); // 获取图形队列
    std::cout<<"Graphics queue: OK\n";

    vkGetDeviceQueue(g_Device, indices.presentFamily.value(), 0, &g_PresentQueue); // 获取呈现队列
    std::cout<<"Present queue: OK\n";
}

void createSwapChain(){
    SwapChainSupportDetails swapChainSupport = querySwapChainSupport(g_PhysicalDevice); // 查询交换链支持信息

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

    QueueFamilyIndices indices = findQueueFamilies(g_PhysicalDevice); // 查找队列族索引
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

    if(vkCreateSwapchainKHR(g_Device, &createInfo, nullptr, &g_SwapChain) != VK_SUCCESS){ // 创建交换链
        throw std::runtime_error("Failed to create swap chain!"); // 失败
    }

    vkGetSwapchainImagesKHR(g_Device, g_SwapChain, &imageCount, nullptr); // 获取交换链图像数量
    g_SwapChainImages.resize(imageCount); // 调整交换链图像大小
    vkGetSwapchainImagesKHR(g_Device, g_SwapChain, &imageCount, g_SwapChainImages.data()); // 获取交换链图像

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
    g_SwapChainImageViews.resize(g_SwapChainImages.size()); // 调整交换链图像视图大小

    for(size_t i = 0; i < g_SwapChainImages.size(); i++){ // 遍历交换链图像
        VkImageViewCreateInfo createInfo{}; // 图像视图创建信息
        createInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO; // 结构体类型
        createInfo.image = g_SwapChainImages[i]; // 图像
        createInfo.viewType = VK_IMAGE_VIEW_TYPE_2D; // 图像视图类型
        createInfo.format = g_SwapChainImageFormat; // 图像格式

        createInfo.components.r = VK_COMPONENT_SWIZZLE_IDENTITY; // 红色分量
        createInfo.components.g = VK_COMPONENT_SWIZZLE_IDENTITY; // 绿色分量    
        createInfo.components.b = VK_COMPONENT_SWIZZLE_IDENTITY; // 蓝色分量
        createInfo.components.a = VK_COMPONENT_SWIZZLE_IDENTITY; // 透明度分量

        createInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT; // 图像视图范围
        createInfo.subresourceRange.baseMipLevel = 0; // 基础 mipmap 级别
        createInfo.subresourceRange.levelCount = 1; // mipmap 级别数量
        createInfo.subresourceRange.baseArrayLayer = 0; // 基础数组层
        createInfo.subresourceRange.layerCount = 1; // 数组层数量

        if(vkCreateImageView(g_Device, &createInfo, nullptr, &g_SwapChainImageViews[i]) != VK_SUCCESS){ // 创建图像视图
            throw std::runtime_error("Failed to create image views!"); // 失败
        }
    }

    std::cout << "Created image views: " << g_SwapChainImageViews.size() << "\n";
    std::cout << "Image views: OK\n"; // 成功
}

void createRenderPass(){
    // VkRenderPassCreateInfo
    // ├── pAttachments → [ VkAttachmentDescription ]   (第 0 个：颜色附件)
    // │
    // ├── pSubpasses → [ VkSubpassDescription ]
    // │                 └── pColorAttachments → [ VkAttachmentReference ]
    // │                         └── attachment = 0     (指向 pAttachments[0])
    // │                             layout = COLOR_ATTACHMENT_OPTIMAL
    // │
    // └── pDependencies → [ VkSubpassDependency ]
    //                     srcSubpass = EXTERNAL
    //                     dstSubpass = 0
    //                     (确保外部操作完成后才开始子过程 0)
    // typedef struct VkAttachmentDescription {
    //     VkAttachmentDescriptionFlags    flags;               // 附件标志
    //     VkFormat                        format;              // 附件格式
    //     VkSampleCountFlagBits           samples;             // 附件样本数
    //     VkAttachmentLoadOp              loadOp;              // 附件加载操作
    //     VkAttachmentStoreOp             storeOp;             // 附件存储操作
    //     VkAttachmentLoadOp              stencilLoadOp;       // 附件深度/模板加载操作
    //     VkAttachmentStoreOp             stencilStoreOp;      // 附件深度/模板存储操作
    //     VkImageLayout                   initialLayout;       // 附件初始布局
    //     VkImageLayout                   finalLayout;         // 附件最终布局
    // } VkAttachmentDescription; // 附件描述
    VkAttachmentDescription colorAttachment{}; // 颜色附件描述, 定义“有哪些图像资源会被这次渲染用到”，以及它们的格式、清屏/保存策略、初始/最终 layout
    colorAttachment.format = g_SwapChainImageFormat; // 颜色附件格式, 使用交换链的格式
    colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT; // 颜色附件样本数, 无多重采样（1x）
    colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR; // 颜色附件加载操作, 开始时清除图像
    colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE; // 颜色附件存储操作, 结束时保存结果
    colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE; // 颜色附件深度/模板加载操作, 不用模板缓冲
    colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE; // 颜色附件深度/模板存储操作
    colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED; // 颜色附件初始布局, 开始前布局无所谓
    colorAttachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR; // 颜色附件最终布局, 结束后适合呈现

    VkAttachmentReference colorAttachmentRef{}; // 颜色附件引用, “子过程要用到 pAttachments 中的哪一个附件，以什么布局使用”
    colorAttachmentRef.attachment = 0; // 颜色附件索引, 引用第 0 号附着（即上面的 colorAttachment）
    colorAttachmentRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL; // 颜色附件布局, 子过程内最佳布局

    VkSubpassDescription subpass{}; // 子过程描述, 定义“一次具体绘制步骤怎么使用 attachment”。这里 graphics pipeline 写入 attachment 0
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS; // 子过程绑定点, 图形管线
    subpass.colorAttachmentCount = 1; // 颜色附件数量, 一个
    subpass.pColorAttachments = &colorAttachmentRef; // 颜色附件引用, 上面的 colorAttachmentRef

    VkSubpassDependency dependency{}; // 子过程依赖, 定义“外部操作和 subpass 之间的同步与 layout transition”。这里保证写 color attachment 前，图像已进入正确状态
    dependency.srcSubpass = VK_SUBPASS_EXTERNAL; // 源子过程, 外部（即没有依赖的子过程）
    dependency.dstSubpass = 0; // 目标子过程, 第 0 号子过程
    dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT; // 源阶段, 颜色附着输出
    dependency.srcAccessMask = 0; // 源访问, 无
    dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT; // 目标阶段, 颜色附着输出
    dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT; // 目标访问, 颜色附着写入

    VkRenderPassCreateInfo renderPassInfo{}; // 呈现过程创建信息, 把所有部分打包成一个完整的渲染通道对象
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO; // 结构体类型
    renderPassInfo.attachmentCount = 1; // 附件数量, 一个
    renderPassInfo.pAttachments = &colorAttachment; // 附件描述, 上面的 colorAttachment
    renderPassInfo.subpassCount = 1; // 子过程数量, 一个
    renderPassInfo.pSubpasses = &subpass; // 子过程描述, 上面的 subpass
    renderPassInfo.dependencyCount = 1; // 子过程依赖数量, 一个
    renderPassInfo.pDependencies = &dependency; // 子过程依赖, 上面的 dependency

    if(vkCreateRenderPass(g_Device, &renderPassInfo, nullptr, &g_RenderPass) != VK_SUCCESS){ // 创建呈现过程
        throw std::runtime_error("Failed to create render pass!"); // 失败    
    }

    std::cout << "Created render pass: OK\n"; // 成功
}
void createGraphicsPipeline(){
    auto vertShaderCode = readFile("shaders/stage1_triangle.vert.spv"); // 读取顶点着色器代码
    auto fragShaderCode = readFile("shaders/stage1_triangle.frag.spv"); // 读取片元着色器代码

    VkShaderModule vertShaderModule = createShaderModule(vertShaderCode); // 创建顶点着色器模块
    VkShaderModule fragShaderModule = createShaderModule(fragShaderCode); // 创建片元着色器模块

    VkPipelineShaderStageCreateInfo vertShaderStageInfo{}; // 顶点着色器阶段信息
    vertShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO; // 结构体类型
    vertShaderStageInfo.stage = VK_SHADER_STAGE_VERTEX_BIT; // 着色器阶段, 顶点着色器
    vertShaderStageInfo.module = vertShaderModule; // 着色器模块, 顶点着色器模块
    vertShaderStageInfo.pName = "main"; // 着色器入口点, main

    VkPipelineShaderStageCreateInfo fragShaderStageInfo{}; // 片元着色器阶段信息
    fragShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO; // 结构体类型
    fragShaderStageInfo.stage = VK_SHADER_STAGE_FRAGMENT_BIT; // 着色器阶段, 片元着色器
    fragShaderStageInfo.module = fragShaderModule; // 着色器模块, 片元着色器模块
    fragShaderStageInfo.pName = "main"; // 着色器入口点, main

    // typedef struct VkPipelineShaderStageCreateInfo {
    //     VkStructureType                     sType;   // 结构体类型
    //     const void*                         pNext;   // 扩展链指针，常为 nullptr
    //     VkPipelineShaderStageCreateFlags    flags;   // 创建标志，常为 0
    //     VkShaderStageFlagBits               stage;   // 着色器阶段
    //     VkShaderModule                      module;  // 着色器模块
    //     const char*                         pName;   // 着色器入口点
    //     const VkSpecializationInfo*         pSpecializationInfo; // 着色器特殊化信息，常为 nullptr
    // } VkPipelineShaderStageCreateInfo;
    VkPipelineShaderStageCreateInfo shaderStages[] = {
        vertShaderStageInfo, 
        fragShaderStageInfo
    }; // 着色器阶段信息数组

    // typedef struct VkVertexInputBindingDescription {
    //     uint32_t             binding;    // 绑定点
    //     uint32_t             stride;     // 顶点数据的步长
    //     VkVertexInputRate    inputRate;  // 顶点输入速率
    // } VkVertexInputBindingDescription;   // 顶点绑定描述，每个元素描述一个顶点缓冲区绑定
    // typedef struct VkVertexInputAttributeDescription {
    //     uint32_t    location;            // 着色器中的位置
    //     uint32_t    binding;             // 绑定点
    //     VkFormat    format;              // 数据格式
    //     uint32_t    offset;              // 数据偏移
    // } VkVertexInputAttributeDescription; // 顶点属性描述, 如位置、颜色、法线、UV
    // typedef struct VkPipelineVertexInputStateCreateInfo {
    //     VkStructureType                             sType;   // 结构体类型
    //     const void*                                 pNext;   // 扩展链指针，常为 nullptr
    //     VkPipelineVertexInputStateCreateFlags       flags;   // 创建标志，常为 0
    //     uint32_t                                    vertexBindingDescriptionCount;   // 顶点绑定描述数量
    //     const VkVertexInputBindingDescription*      pVertexBindingDescriptions;      // 顶点绑定描述数组
    //     uint32_t                                    vertexAttributeDescriptionCount; // 顶点属性描述数量
    //     const VkVertexInputAttributeDescription*    pVertexAttributeDescriptions;    // 顶点属性描述数组
    // } VkPipelineVertexInputStateCreateInfo;
    VkPipelineVertexInputStateCreateInfo vertexInputInfo{}; // 顶点输入信息
    vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO; // 结构体类型
    vertexInputInfo.vertexBindingDescriptionCount = 0; // 顶点绑定描述数量, 无
    vertexInputInfo.pVertexBindingDescriptions = nullptr; // 顶点绑定描述数组, 无
    vertexInputInfo.vertexAttributeDescriptionCount = 0; // 顶点属性描述数量, 无
    vertexInputInfo.pVertexAttributeDescriptions = nullptr; // 顶点属性描述数组, 无

    // typedef struct VkPipelineInputAssemblyStateCreateInfo {
    //     VkStructureType                            sType;    // 结构体类型
    //     const void*                                pNext;    // 扩展链指针，常为 nullptr
    //     VkPipelineInputAssemblyStateCreateFlags    flags;    // 创建标志，常为 0
    //     VkPrimitiveTopology                        topology; // 图元拓扑
    //     VkBool32                                   primitiveRestartEnable; // 图元重启
    // } VkPipelineInputAssemblyStateCreateInfo; // 输入装配信息
    VkPipelineInputAssemblyStateCreateInfo inputAssembly{}; // 输入装配信息
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO; // 结构体类型
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST; // 图元拓扑, 三角形列表
    inputAssembly.primitiveRestartEnable = VK_FALSE; // 图元重启, 禁用

    VkPipelineViewportStateCreateInfo viewportState{}; // 视口状态信息
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO; // 结构体类型
    viewportState.viewportCount = 1; // 视口数量, 一个
    viewportState.pViewports = nullptr; // 视口数组, 无
    viewportState.scissorCount = 1; // 裁剪区域数量, 一个
    viewportState.pScissors = nullptr; // 裁剪区域数组, 无

    VkPipelineRasterizationStateCreateInfo rasterizer{}; // 光栅化信息
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO; // 结构体类型
    rasterizer.depthClampEnable = VK_FALSE; // 深度剪裁, 禁用
    rasterizer.rasterizerDiscardEnable = VK_FALSE; // 光栅化丢弃, 禁用
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL; // 多边形模式, 填充
    rasterizer.lineWidth = 1.0f; // 线宽, 1.0
    rasterizer.cullMode = VK_CULL_MODE_NONE; // 剔除模式, 无
    rasterizer.frontFace = VK_FRONT_FACE_CLOCKWISE; // 正面朝向, 顺时针
    rasterizer.depthBiasEnable = VK_FALSE; // 深度偏移, 禁用

    VkPipelineMultisampleStateCreateInfo multisampling{}; // 多重采样信息
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO; // 结构体类型
    multisampling.sampleShadingEnable = VK_FALSE; // 样本着色, 禁用
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT; // 样本数, 1x

    VkPipelineColorBlendAttachmentState colorBlendAttachment{}; // 颜色混合附着描述
    colorBlendAttachment.colorWriteMask = 
        VK_COLOR_COMPONENT_R_BIT | 
        VK_COLOR_COMPONENT_G_BIT | 
        VK_COLOR_COMPONENT_B_BIT | 
        VK_COLOR_COMPONENT_A_BIT; // 颜色写入掩码, RGBA
    colorBlendAttachment.blendEnable = VK_FALSE; // 混合启用, 禁用

    VkPipelineColorBlendStateCreateInfo colorBlending{}; // 颜色混合信息
    colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO; // 结构体类型
    colorBlending.logicOpEnable = VK_FALSE; // 逻辑操作启用, 禁用
    colorBlending.logicOp = VK_LOGIC_OP_COPY; // 逻辑操作, 复制
    colorBlending.attachmentCount = 1; // 附着数量, 一个
    colorBlending.pAttachments = &colorBlendAttachment; // 附着描述, 上面的 colorBlendAttachment

    std::vector<VkDynamicState> dynamicStates = {
        VK_DYNAMIC_STATE_VIEWPORT, // 视口
        VK_DYNAMIC_STATE_SCISSOR // 裁剪区域
    };

    VkPipelineDynamicStateCreateInfo dynamicState{}; // 动态状态信息
    dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO; // 结构体类型
    dynamicState.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size()); // 动态状态数量
    dynamicState.pDynamicStates = dynamicStates.data(); // 动态状态数组

    VkPipelineLayoutCreateInfo pipelineLayoutInfo{}; // 管线布局信息
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO; // 结构体类型
    pipelineLayoutInfo.setLayoutCount = 0; // 布局数量, 无
    pipelineLayoutInfo.pSetLayouts = nullptr; // 布局数组, 无
    pipelineLayoutInfo.pushConstantRangeCount = 0; // 推送常量范围数量, 无

    if(vkCreatePipelineLayout(g_Device, &pipelineLayoutInfo, nullptr, &g_PipelineLayout) != VK_SUCCESS){ // 创建管线布局
        throw std::runtime_error("Failed to create pipeline layout!"); // 失败
    }

    // typedef struct VkGraphicsPipelineCreateInfo {
    //     VkStructureType                                  sType;                  // 结构体类型
    //     const void*                                      pNext;                  // 扩展链指针，常为 nullptr
    //     VkPipelineCreateFlags                            flags;                  // 创建标志，常为 0
    //     uint32_t                                         stageCount;             // 着色器阶段数量
    //     const VkPipelineShaderStageCreateInfo*           pStages;                // 着色器阶段数组
    //     const VkPipelineVertexInputStateCreateInfo*      pVertexInputState;      // 顶点输入信息
    //     const VkPipelineInputAssemblyStateCreateInfo*    pInputAssemblyState;    // 输入装配信息
    //     const VkPipelineTessellationStateCreateInfo*     pTessellationState;     // 细分控制信息
    //     const VkPipelineViewportStateCreateInfo*         pViewportState;         // 视口状态信息
    //     const VkPipelineRasterizationStateCreateInfo*    pRasterizationState;    // 光栅化信息
    //     const VkPipelineMultisampleStateCreateInfo*      pMultisampleState;      // 多重采样信息
    //     const VkPipelineDepthStencilStateCreateInfo*     pDepthStencilState;     // 深度模板状态信息
    //     const VkPipelineColorBlendStateCreateInfo*       pColorBlendState;       // 颜色混合信息
    //     const VkPipelineDynamicStateCreateInfo*          pDynamicState;          // 动态状态信息
    //     VkPipelineLayout                                 layout;                 // 管线布局
    //     VkRenderPass                                     renderPass;             // 渲染通道
    //     uint32_t                                         subpass;                // 子通道
    //     VkPipeline                                       basePipelineHandle;     // 基础管线
    //     int32_t                                          basePipelineIndex;      // 基础管线索引
    // } VkGraphicsPipelineCreateInfo;  // 图形管线创建信息
    VkGraphicsPipelineCreateInfo pipelineInfo{}; // 管线信息
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO; // 结构体类型
    pipelineInfo.stageCount = 2; // 着色器阶段数量, 两个
    pipelineInfo.pStages = shaderStages; // 着色器阶段数组, 上面的 shaderStages
    pipelineInfo.pVertexInputState = &vertexInputInfo; // 顶点输入信息, 上面的 vertexInputInfo
    pipelineInfo.pInputAssemblyState = &inputAssembly; // 输入装配信息, 上面的 inputAssembly, 指定顶点以什么方式连接
    pipelineInfo.pViewportState = &viewportState; // 视口状态信息, 上面的 viewportState
    pipelineInfo.pRasterizationState = &rasterizer; // 光栅化信息, 上面的 rasterizer
    pipelineInfo.pMultisampleState = &multisampling; // 多重采样信息, 上面的 multisampling, 多重采样抗锯齿（MSAA）
    pipelineInfo.pDepthStencilState = nullptr; // 深度模板状态信息, 无, 深度测试和模板测试
    pipelineInfo.pColorBlendState = &colorBlending; // 颜色混合信息, 上面的 colorBlending, 片段着色器输出如何与帧缓冲现有颜色混合
    pipelineInfo.pDynamicState = &dynamicState; // 动态状态信息, 上面的 dynamicState, 指定哪些状态可以在不重建管线的情况下动态更改
    pipelineInfo.layout = g_PipelineLayout; // 管线布局, 上面的 g_PipelineLayout, 管线布局决定了着色器如何访问资源
    pipelineInfo.renderPass = g_RenderPass; // 渲染通道, 上面的 g_RenderPass, 渲染通道决定了管线如何与帧缓冲交互
    pipelineInfo.subpass = 0; // 子通道, 0
    pipelineInfo.basePipelineHandle = VK_NULL_HANDLE; // 基础管线, 无
    pipelineInfo.basePipelineIndex = -1; // 基础管线索引, -1

    if(vkCreateGraphicsPipelines(g_Device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &g_GraphicsPipeline) != VK_SUCCESS){ // 创建管线
        throw std::runtime_error("Failed to create graphics pipeline!"); // 失败    
    }

    vkDestroyShaderModule(g_Device, fragShaderModule, nullptr);
    vkDestroyShaderModule(g_Device, vertShaderModule, nullptr);

    std::cout << "Created graphics pipeline: OK\n"; // 成功
}
std::vector<char> readFile(const std::string& filename){ // 读取文件
    std::ifstream file(filename, std::ios::ate | std::ios::binary); // 以二进制模式打开文件，从末尾开始读取
    if(!file.is_open()){ // 如果文件未打开
        throw std::runtime_error("Failed to open file: " + filename); // 抛出异常
    }

    size_t fileSize = (size_t)file.tellg(); // 获取文件大小
    std::vector<char> buffer(fileSize); // 创建缓冲区

    file.seekg(0); // 从文件开头开始读取
    file.read(buffer.data(), fileSize); // 读取文件
    file.close(); // 关闭文件

    return buffer; // 返回缓冲区
}
VkShaderModule createShaderModule(const std::vector<char>& code){ // 创建着色器模块
    VkShaderModuleCreateInfo createInfo{}; // 着色器模块创建信息
    createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO; // 结构体类型
    createInfo.codeSize = code.size(); // 着色器代码大小
    createInfo.pCode = reinterpret_cast<const uint32_t*>(code.data()); // 着色器代码指针, pCode 要 uint32_t*，因为 SPIR-V 按 32-bit word 存储
    
    VkShaderModule shaderModule = VK_NULL_HANDLE; // 着色器模块

    // VkResult vkCreateShaderModule(
    //     VkDevice                                    device,          // 逻辑设备
    //     const VkShaderModuleCreateInfo*             pCreateInfo,     // 着色器模块创建信息
    //     const VkAllocationCallbacks*                pAllocator,      // 自定义内存分配器，常为 nullptr
    //     VkShaderModule*                             pShaderModule    // 输出：着色器模块句柄
    // );
    if(vkCreateShaderModule(g_Device, &createInfo, nullptr, &shaderModule) != VK_SUCCESS){ // 创建着色器模块
        throw std::runtime_error("Failed to create shader module!"); // 失败    
    }

    return shaderModule;
}

void createFramebuffers(){ // 创建帧缓冲区
    g_SwapChainFramebuffers.resize(g_SwapChainImageViews.size()); // 帧缓冲区大小

    for(size_t i = 0; i < g_SwapChainImageViews.size(); i++){ // 遍历交换链图像视图
        VkImageView attachments[] = { // 帧缓冲区附件
            g_SwapChainImageViews[i] // 交换链图像视图
        };
        
        VkFramebufferCreateInfo framebufferInfo{}; // 帧缓冲区创建信息
        framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO; // 结构体类型
        framebufferInfo.renderPass = g_RenderPass; // 渲染通道
        framebufferInfo.attachmentCount = 1; // 附件数量
        framebufferInfo.pAttachments = attachments; // 附件数组
        framebufferInfo.width = g_SwapChainExtent.width; // 帧缓冲区宽度
        framebufferInfo.height = g_SwapChainExtent.height; // 帧缓冲区高度
        framebufferInfo.layers = 1; // 层数量

        if(vkCreateFramebuffer(g_Device, &framebufferInfo, nullptr, &g_SwapChainFramebuffers[i]) != VK_SUCCESS){ // 创建帧缓冲区
            throw std::runtime_error("Failed to create framebuffer!"); // 失败
        }
    }

    std::cout << "Created framebuffers: " << g_SwapChainFramebuffers.size() << "\n"; // 成功
}
void createCommandPool(){ // 创建命令池
    QueueFamilyIndices queueFamilyIndices = findQueueFamilies(g_PhysicalDevice); // 队列族索引

    VkCommandPoolCreateInfo poolInfo{}; // 命令池创建信息
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO; // 结构体类型
    poolInfo.queueFamilyIndex = queueFamilyIndices.graphicsFamily.value(); // 队列族索引
    poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT; // 命令缓冲区重置标志

    if(vkCreateCommandPool(g_Device, &poolInfo, nullptr, &g_CommandPool) != VK_SUCCESS){ // 创建命令池
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

    if(vkAllocateCommandBuffers(g_Device, &allocInfo, g_CommandBuffers.data()) != VK_SUCCESS){ // 分配命令缓冲区
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
        if(vkCreateSemaphore(g_Device, &semaphoreInfo, nullptr, &g_ImageAvailableSemaphores[i]) != VK_SUCCESS || // 创建图像可用信号量
           vkCreateSemaphore(g_Device, &semaphoreInfo, nullptr, &g_RenderFinishedSemaphores[i]) != VK_SUCCESS || // 创建渲染完成信号量
           vkCreateFence(g_Device, &fenceInfo, nullptr, &g_InFlightFences[i]) != VK_SUCCESS){ // 创建并发帧信号量
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

    vkWaitForFences(g_Device, 1, &g_InFlightFences[g_CurrentFrame], VK_TRUE, UINT64_MAX); // 等待栅栏

    uint32_t imageIndex = 0; // 图像索引
    // VkResult vkAcquireNextImageKHR(
    //     VkDevice                                    device,      // 设备
    //     VkSwapchainKHR                              swapchain,   // 交换链
    //     uint64_t                                    timeout,     // 超时时间
    //     VkSemaphore                                 semaphore,   // 信号量
    //     VkFence                                     fence,       // 栅栏
    //     uint32_t*                                   pImageIndex  // 图像索引
    // ); // 获取下一个图像
    VkResult result = vkAcquireNextImageKHR(g_Device, g_SwapChain, UINT64_MAX, g_ImageAvailableSemaphores[g_CurrentFrame], VK_NULL_HANDLE, &imageIndex); // 等待图像可用

    if(result == VK_ERROR_OUT_OF_DATE_KHR){ // 交换链已过期
        recreateSwapChain(); // 重新创建交换链
        return;    
    }
    else if(result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR){ // 其他错误
        throw std::runtime_error("Failed to acquire swap chain image!"); // 失败
    }

    vkResetFences(g_Device, 1, &g_InFlightFences[g_CurrentFrame]); // 重置栅栏, 防止上一帧的栅栏未完成导致当前帧无法开始

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
    if(vkQueueSubmit(g_GraphicsQueue, 1, &submitInfo, g_InFlightFences[g_CurrentFrame]) != VK_SUCCESS){ // 提交命令缓冲区
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
    result = vkQueuePresentKHR(g_PresentQueue, &presentInfo); // 呈现图像

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
    renderPassInfo.renderPass = g_RenderPass; // 渲染通道
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
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, g_GraphicsPipeline); // 绑定管线

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

    vkDeviceWaitIdle(g_Device); // 等待设备空闲

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
        vkDestroyFramebuffer(g_Device, framebuffer, nullptr); // 销毁帧缓冲区    
    }
    g_SwapChainFramebuffers.clear(); // 清空帧缓冲区数组

    if(g_GraphicsPipeline != VK_NULL_HANDLE){ // 销毁图形管线
        vkDestroyPipeline(g_Device, g_GraphicsPipeline, nullptr); // 销毁图形管线
        g_GraphicsPipeline = VK_NULL_HANDLE; // 置空图形管线
    }

    if(g_PipelineLayout != VK_NULL_HANDLE){ // 销毁管线布局
        vkDestroyPipelineLayout(g_Device, g_PipelineLayout, nullptr); // 销毁管线布局
        g_PipelineLayout = VK_NULL_HANDLE; // 置空管线布局
    }

    if(g_RenderPass != VK_NULL_HANDLE){ // 销毁渲染通道
        vkDestroyRenderPass(g_Device, g_RenderPass, nullptr); // 销毁渲染通道
        g_RenderPass = VK_NULL_HANDLE; // 置空渲染通道
    }

    for(auto imageView : g_SwapChainImageViews){ // 销毁图像视图
        vkDestroyImageView(g_Device, imageView, nullptr); // 销毁图像视图
    }
    g_SwapChainImageViews.clear(); // 清空图像视图数组

    if(g_SwapChain != VK_NULL_HANDLE){ // 销毁交换链
        vkDestroySwapchainKHR(g_Device, g_SwapChain, nullptr); // 销毁交换链
        g_SwapChain = VK_NULL_HANDLE; // 置空交换链
    }
}