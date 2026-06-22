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
#include <algorithm> // clamp
#include <cstdint> // uint32_t
#include <limits> // UINT32_MAX


GLFWwindow* g_Window = nullptr;

// VkInstance
//   ├── VkSurfaceKHR (独立句柄，由平台窗口创建)
//   ├── VkPhysicalDevice (GPU 选择)
//   └── VkDevice (逻辑设备)
//         └── VkSwapchainKHR (依赖 VkDevice 和 VkSurfaceKHR)
VkInstance g_Instance = VK_NULL_HANDLE;                         // Vulkan 实例
VkDebugUtilsMessengerEXT g_DebugMessenger = VK_NULL_HANDLE;     // 调试回调，用于接收来自 Vulkan 校验层和驱动的诊断消息
VkSurfaceKHR g_Surface = VK_NULL_HANDLE;                        // 窗口表面，用于呈现图像到窗口
VkPhysicalDevice g_PhysicalDevice = VK_NULL_HANDLE;             // 物理设备，即 GPU
VkDevice g_Device = VK_NULL_HANDLE;                             // 逻辑设备，用于执行 Vulkan 命令
VkQueue g_GraphicsQueue = VK_NULL_HANDLE;                       // 图形队列，用于执行图形命令
VkQueue g_PresentQueue = VK_NULL_HANDLE;                        // 呈现队列，用于呈现图像到窗口
VkSwapchainKHR g_SwapChain = VK_NULL_HANDLE;                    // 交换链，用于管理呈现到屏幕的图像缓冲区序列
std::vector<VkImage> g_SwapChainImages;                         // 交换链图像，用于存储呈现到屏幕的图像缓冲区
VkFormat g_SwapChainImageFormat;                                // 交换链图像格式，用于存储呈现到屏幕的图像缓冲区的格式
VkExtent2D g_SwapChainExtent;                                   // 交换链图像大小，用于存储呈现到屏幕的图像缓冲区的大小
std::vector<VkImageView> g_SwapChainImageViews;                 // 交换链图像视图，用于存储呈现到屏幕的图像缓冲区的视图

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
    bool isComplete(){
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

void createInstance(); // 创建 Vulkan 实例

void setupDebugMessenger(); // 设置调试回调
// Vulkan 调试回调 用于接收来自 Vulkan 校验层和驱动的诊断消息
VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback( // VKAPI_ATTR 是一个宏，用于标记函数为 Vulkan API 的一部分；VKAPI_CALL 是一个宏，用于指定函数的调用约定，服务全平台
    VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity, // 消息的严重程度
    VkDebugUtilsMessageTypeFlagsEXT messageType, // 消息的类型
    const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData, // 包含消息详细信息的结构体指针
    void* pUserData // 用户自定义数据，通常用于传递额外的上下文信息
);
bool checkValidationLayerSupport(); // 检查是否支持校验层
std::vector<const char*> getRequiredExtensions(); // 获取所需的扩展
void populateDebugMessengerCreateInfo(VkDebugUtilsMessengerCreateInfoEXT& createInfo); // 填充 VkDebugUtilsMessengerCreateInfoEXT 结构体
VkResult createDebugUtilsMessengerEXT( // 创建调试回调
    VkInstance instance, // Vulkan 实例
    const VkDebugUtilsMessengerCreateInfoEXT* pCreateInfo, // 调试回调创建信息
    const VkAllocationCallbacks* pAllocator, // 内存分配器
    VkDebugUtilsMessengerEXT* pDebugMessenger // 输出：调试回调句柄    
);
void destroyDebugUtilsMessengerEXT( // 销毁调试回调
    VkInstance instance, // Vulkan 实例
    VkDebugUtilsMessengerEXT debugMessenger, // 调试回调句柄
    const VkAllocationCallbacks* pAllocator // 内存分配器
);

void createSurface();           // 创建窗口表面

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
void createFramebuffers();      // 创建帧缓冲区
void createCommandPool();       // 创建命令池
void createCommandBuffers();    // 创建命令缓冲区
void createSyncObjects();       // 创建同步对象

void drawFrame();               // 绘制帧
void recordCommandBuffer(VkCommandBuffer commandBuffer, uint32_t imageIndex);   // 记录命令缓冲区

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
}
void initVulkan(){ // 初始化 Vulkan
    createInstance();       // 创建实例
    setupDebugMessenger();  // 设置调试回调
    createSurface();        // 创建窗口表面
    pickPhysicalDevice();   // 选择物理设备
    createLogicalDevice();  // 创建逻辑设备
    createSwapChain();      // 创建交换链
    createImageViews();     // 创建图像视图
}
void mainLoop(){ // 主循环
    while(!glfwWindowShouldClose(g_Window)){
        glfwPollEvents(); // 处理所有等待中的事件
        if(glfwGetKey(g_Window, GLFW_KEY_ESCAPE) == GLFW_PRESS){ // 按下esc键
            glfwSetWindowShouldClose(g_Window, GLFW_TRUE);
        }
    }
}
void cleanup(){  // 清理资源
    for(auto imageView : g_SwapChainImageViews){ // 销毁图像视图
        vkDestroyImageView(g_Device, imageView, nullptr);
    }
    if(g_SwapChain != VK_NULL_HANDLE){
        vkDestroySwapchainKHR(g_Device, g_SwapChain, nullptr); // 销毁交换链
    }
    if(g_Device != VK_NULL_HANDLE){
        vkDestroyDevice(g_Device, nullptr); // 销毁逻辑设备, Device 必须早于 Surface/Instance 销毁
    }
    if(g_Surface != VK_NULL_HANDLE){
        vkDestroySurfaceKHR(g_Instance, g_Surface, nullptr); // 销毁窗口表面
    }
    if(g_DebugMessenger != VK_NULL_HANDLE){
        destroyDebugUtilsMessengerEXT(g_Instance, g_DebugMessenger, nullptr); // 销毁调试回调
    }
    if(g_Instance != VK_NULL_HANDLE){
        vkDestroyInstance(g_Instance, nullptr); // 销毁实例
    }
    glfwDestroyWindow(g_Window); // 销毁窗口
    glfwTerminate(); // 终止 GLFW
}


void createInstance(){ // 创建实例
    if(g_EnableValidationLayers && !checkValidationLayerSupport()){ // 检查是否支持校验层
        throw std::runtime_error("Validation layers requested, but not available!");
    }
    if(g_EnableValidationLayers){
        std::cout<<"Validation layers enabled\n";
    }
    else{
        std::cout<<"Validation layers disabled\n";
    }

    VkApplicationInfo appInfo{}; // 应用程序信息
    appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO; // 结构体类型
    appInfo.pApplicationName = "Stage 1 - Vulkan Triangle"; // 应用程序名称
    appInfo.applicationVersion = VK_MAKE_VERSION(1,0,0); // 应用程序版本
    appInfo.pEngineName = "No Engine"; // 引擎名称
    appInfo.engineVersion = VK_MAKE_VERSION(1,0,0); // 引擎版本
    appInfo.apiVersion = VK_API_VERSION_1_3; // Vulkan API 版本

    auto extensions = getRequiredExtensions(); // 获取所需的扩展

    VkInstanceCreateInfo createInfo{}; // 实例创建信息
    createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO; // 结构体类型
    createInfo.pApplicationInfo = &appInfo; // 应用程序信息
    createInfo.enabledExtensionCount = static_cast<uint32_t>(extensions.size()); // 启用的扩展数量
    createInfo.ppEnabledExtensionNames = extensions.data(); // 启用的扩展名称数组

    VkDebugUtilsMessengerCreateInfoEXT debugCreateInfo{}; // 调试回调创建信息
    if(g_EnableValidationLayers){ // 如果启用校验层
        createInfo.enabledLayerCount = static_cast<uint32_t>(g_ValidationLayers.size()); // 启用的校验层数量
        createInfo.ppEnabledLayerNames = g_ValidationLayers.data(); // 启用的校验层名称数组
        populateDebugMessengerCreateInfo(debugCreateInfo); // 填充调试回调创建信息
        createInfo.pNext = (VkDebugUtilsMessengerCreateInfoEXT*)&debugCreateInfo; // 将调试回调创建信息链接到实例创建信息
    }
    else{
        createInfo.enabledLayerCount = 0; // 启用的校验层数量为0
        createInfo.pNext = nullptr; // 没有调试回调
    }

    if(vkCreateInstance(&createInfo, nullptr, &g_Instance) != VK_SUCCESS){ // 创建实例
        throw std::runtime_error("Failed to create Vulkan instance!");
    }

    std::cout<<"Vulkan instance created: OK\n";
}

void setupDebugMessenger(){ // 设置调试回调
    if(!g_EnableValidationLayers){ // 如果不启用校验层
        return;
    }
    VkDebugUtilsMessengerCreateInfoEXT createInfo{}; // 调试回调创建信息
    populateDebugMessengerCreateInfo(createInfo); // 填充调试回调创建信息
    if(createDebugUtilsMessengerEXT(g_Instance, &createInfo, nullptr, &g_DebugMessenger) != VK_SUCCESS){ // 创建调试回调
        throw std::runtime_error("Failed to set up debug messenger!");
    }
    std::cout<<"Debug messenger created: OK\n";
}
VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback( // Vulkan 调试回调 用于接收来自 Vulkan 校验层和驱动的诊断消息
    // VKAPI_ATTR 是一个宏，用于标记函数为 Vulkan API 的一部分
    // VKAPI_CALL 是一个宏，用于指定函数的调用约定，服务全平台
    VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity, // 消息的严重程度
    VkDebugUtilsMessageTypeFlagsEXT messageType, // 消息的类型
    const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData, // 包含消息详细信息的结构体指针
    void* pUserData // 用户自定义数据，通常用于传递额外的上下文信息
){
    std::cerr << "Validation layer: " << pCallbackData->pMessage << "\n"; // 输出消息
    return VK_FALSE; // 返回 VK_FALSE 表示不终止程序
}
bool checkValidationLayerSupport(){ // 检查是否支持校验层
    uint32_t layerCount = 0;
    vkEnumerateInstanceLayerProperties(&layerCount, nullptr);
    std::vector<VkLayerProperties> availableLayers(layerCount);
    vkEnumerateInstanceLayerProperties(&layerCount, availableLayers.data());

    for(const char* layerName:g_ValidationLayers){
        bool layerFound = false;
        for(const auto& layerProperties:availableLayers){
            if(strcmp(layerName, layerProperties.layerName) == 0){
                layerFound = true;
                break;
            }
        }
        if(!layerFound){
            return false;
        }
    }
    return true;
}
std::vector<const char*> getRequiredExtensions(){ // 获取所需的扩展
    uint32_t glfwExtensionCount = 0;
    const char** glfwExtensions = glfwGetRequiredInstanceExtensions(&glfwExtensionCount); // 获取 GLFW 所需的扩展
    std::vector<const char*> extensions(glfwExtensions, glfwExtensions + glfwExtensionCount); // 将 GLFW 所需的扩展复制到 std::vector
    if(g_EnableValidationLayers){
        extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME); // 添加 VK_EXT_DEBUG_UTILS_EXTENSION_NAME 扩展
    }
    std::cout<<"Required extensions:\n";
    for(const auto& extension:extensions){
        std::cout<<"  "<<extension<<"\n";
    }
    return extensions;
}
void populateDebugMessengerCreateInfo(VkDebugUtilsMessengerCreateInfoEXT& createInfo){ // 填充调试回调创建信息
    createInfo = {};
    createInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT; // 结构体类型
    createInfo.messageSeverity =  // 消息严重程度
        VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT |  // 详细消息
        VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |  // 警告消息
        VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;     // 错误消息
    createInfo.messageType = // 消息类型
        VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |    // 一般消息
        VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT | // 校验消息
        VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT; // 性能消息
    createInfo.pfnUserCallback = debugCallback; // 回调函数
    createInfo.pUserData = nullptr; // 用户自定义数据
}
VkResult createDebugUtilsMessengerEXT( // 创建调试回调
    VkInstance instance, // Vulkan 实例
    const VkDebugUtilsMessengerCreateInfoEXT* pCreateInfo, // 调试回调创建信息
    const VkAllocationCallbacks* pAllocator, // 内存分配器
    VkDebugUtilsMessengerEXT* pDebugMessenger // 输出：调试回调句柄    
){
    auto func = reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>( // 从实例中获取函数指针
        vkGetInstanceProcAddr(instance, "vkCreateDebugUtilsMessengerEXT") // 获取函数指针
    );
    if(func != nullptr){ // 如果函数指针不为空
        return func(instance, pCreateInfo, pAllocator, pDebugMessenger); // 调用函数
    }
    return VK_ERROR_EXTENSION_NOT_PRESENT; // 返回 VK_ERROR_EXTENSION_NOT_PRESENT 表示扩展未找到
}
void destroyDebugUtilsMessengerEXT( // 销毁调试回调
    VkInstance instance, // Vulkan 实例
    VkDebugUtilsMessengerEXT debugMessenger, // 调试回调句柄
    const VkAllocationCallbacks* pAllocator // 内存分配器
){
    auto func = reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>( // 从实例中获取函数指针
        vkGetInstanceProcAddr(instance, "vkDestroyDebugUtilsMessengerEXT") // 获取函数指针
    );
    if(func != nullptr){ // 如果函数指针不为空
        func(instance, debugMessenger, pAllocator); // 调用函数
    }
}

void createSurface(){ // 创建窗口表面
    // VkResult glfwCreateWindowSurface(
    //     VkInstance instance,                // Vulkan 实例
    //     GLFWwindow* window,                 // GLFW 窗口
    //     const VkAllocationCallbacks* allocator, // 内存分配器（常为 nullptr）
    //     VkSurfaceKHR* surface               // 输出：创建的 Surface 句柄
    // ); // glfw 做好了封装
    if(glfwCreateWindowSurface(g_Instance, g_Window, nullptr, &g_Surface) != VK_SUCCESS){ // 创建窗口表面
        throw std::runtime_error("Failed to create window surface!");
    }
    std::cout<<"Window surface created: OK\n";
}

void pickPhysicalDevice(){ // 选择物理设备
    uint32_t deviceCount = 0; // 设备数量
    vkEnumeratePhysicalDevices(g_Instance, &deviceCount, nullptr); // 获取设备数量

    if(deviceCount == 0){ // 如果设备数量为 0
        throw std::runtime_error("Failed to find GPUs with Vulkan support!"); // 抛出异常
    }

    std::vector<VkPhysicalDevice> devices(deviceCount); // 设备数组
    vkEnumeratePhysicalDevices(g_Instance, &deviceCount, devices.data()); // 获取设备数组
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
        vkGetPhysicalDeviceSurfaceSupportKHR(device, i, g_Surface, &presentSupport); // 获取是否支持呈现
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

    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(device, g_Surface, &details.capabilities); // 获取交换链能力
    
    uint32_t formatCount = 0; // 图像格式数量
    vkGetPhysicalDeviceSurfaceFormatsKHR(device, g_Surface, &formatCount, nullptr); // 获取图像格式数量
    if(formatCount != 0){ // 如果图像格式数量不为 0
        details.formats.resize(formatCount); // 调整图像格式数组大小    
        vkGetPhysicalDeviceSurfaceFormatsKHR(device, g_Surface, &formatCount, details.formats.data()); // 获取图像格式数组
    }    
    
    uint32_t presentModeCount = 0; // 呈现模式数量
    vkGetPhysicalDeviceSurfacePresentModesKHR(device, g_Surface, &presentModeCount, nullptr); // 获取呈现模式数量
    if(presentModeCount != 0){ // 如果呈现模式数量不为 0
        details.presentModes.resize(presentModeCount); // 调整呈现模式数组大小
        vkGetPhysicalDeviceSurfacePresentModesKHR(device, g_Surface, &presentModeCount, details.presentModes.data()); // 获取呈现模式数组
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
    createInfo.surface = g_Surface; // 表面
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
        std::cerr << "Failed to create swap chain!\n"; // 失败
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
void createRenderPass(){}
void createGraphicsPipeline(){}
void createFramebuffers(){}
void createCommandPool(){}
void createCommandBuffers(){}
void createSyncObjects(){}


void drawFrame(){}
void recordCommandBuffer(VkCommandBuffer commandBuffer, uint32_t imageIndex){}


void recreateSwapChain(){}
void cleanupSwapChain(){}