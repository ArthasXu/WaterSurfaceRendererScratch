#include "Instance.h"

#include <GLFW/glfw3.h>

#include <cstring>
#include <iostream>
#include <stdexcept> // std::runtime_error

namespace // 匿名命名空间，用于定义只在本文件中可见的变量和函数
{
    const std::vector<const char*> ValidationLayers = {"VK_LAYER_KHRONOS_validation"}; // 校验层
};

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

namespace vkp
{
Instance::Instance(bool enableValidationLayers) : m_EnableValidationLayers(enableValidationLayers) // 初始化成员变量
{
    createInstance(); // 创建 Vulkan 实例
    setupDebugMessenger(); // 设置调试回调
}

Instance::~Instance(){ // 析构函数
    if(m_DebugMessenger != VK_NULL_HANDLE){ // 如果调试回调不为空
        destroyDebugUtilsMessengerEXT(m_Instance, m_DebugMessenger, nullptr); // 销毁调试回调
    }

    if(m_Instance != VK_NULL_HANDLE){ // 如果实例不为空
        vkDestroyInstance(m_Instance, nullptr); // 销毁实例
    }
}

Instance::operator VkInstance() const { // 隐式类型转换，用于将 Instance 转换为 VkInstance
    return m_Instance;
}

VkInstance Instance::get() const { // 获取 Vulkan 实例
    return m_Instance;
}

void Instance::createInstance(){ // 创建 Vulkan 实例
    if(m_EnableValidationLayers && !checkValidationLayerSupport()){ // 检查是否支持校验层
        throw std::runtime_error("Validation layers requested, but not available!");
    }
    if(m_EnableValidationLayers){
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
    if(m_EnableValidationLayers){ // 如果启用校验层
        createInfo.enabledLayerCount = static_cast<uint32_t>(ValidationLayers.size()); // 启用的校验层数量
        createInfo.ppEnabledLayerNames = ValidationLayers.data(); // 启用的校验层名称数组
        populateDebugMessengerCreateInfo(debugCreateInfo); // 填充调试回调创建信息
        createInfo.pNext = (VkDebugUtilsMessengerCreateInfoEXT*)&debugCreateInfo; // 将调试回调创建信息链接到实例创建信息
    }
    else{
        createInfo.enabledLayerCount = 0; // 启用的校验层数量为0
        createInfo.pNext = nullptr; // 没有调试回调
    }

    if(vkCreateInstance(&createInfo, nullptr, &m_Instance) != VK_SUCCESS){ // 创建实例
        throw std::runtime_error("Failed to create Vulkan instance!");
    }

    std::cout<<"Vulkan instance created: OK\n";
}

void Instance::setupDebugMessenger(){ // 设置调试回调
    if(!m_EnableValidationLayers){ // 如果不启用校验层
        return;
    }
    VkDebugUtilsMessengerCreateInfoEXT createInfo{}; // 调试回调创建信息
    populateDebugMessengerCreateInfo(createInfo); // 填充调试回调创建信息
    if(createDebugUtilsMessengerEXT(m_Instance, &createInfo, nullptr, &m_DebugMessenger) != VK_SUCCESS){ // 创建调试回调
        throw std::runtime_error("Failed to set up debug messenger!");
    }
    std::cout<<"Debug messenger created: OK\n";
}

bool Instance::checkValidationLayerSupport() const { // 检查是否支持校验层
    uint32_t layerCount = 0;
    vkEnumerateInstanceLayerProperties(&layerCount, nullptr);
    std::vector<VkLayerProperties> availableLayers(layerCount);
    vkEnumerateInstanceLayerProperties(&layerCount, availableLayers.data());

    for(const char* layerName : ValidationLayers){
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

std::vector<const char*> Instance::getRequiredExtensions() const { // 获取所需的扩展
    uint32_t glfwExtensionCount = 0;
    const char** glfwExtensions = glfwGetRequiredInstanceExtensions(&glfwExtensionCount); // 获取 GLFW 所需的扩展
    std::vector<const char*> extensions(glfwExtensions, glfwExtensions + glfwExtensionCount); // 将 GLFW 所需的扩展复制到 std::vector
    if(m_EnableValidationLayers){
        extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME); // 添加 VK_EXT_DEBUG_UTILS_EXTENSION_NAME 扩展
    }
    std::cout<<"Required extensions:\n";
    for(const auto& extension:extensions){
        std::cout<<"  "<<extension<<"\n";
    }
    return extensions;
}

void Instance::populateDebugMessengerCreateInfo(VkDebugUtilsMessengerCreateInfoEXT& createInfo) const { // 填充调试回调创建信息
    createInfo = {};
    createInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT; // 结构体类型
    createInfo.messageSeverity =  // 消息严重程度
        // VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT |  // 详细消息
        VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |  // 警告消息
        VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;     // 错误消息
    createInfo.messageType = // 消息类型
        VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |    // 一般消息
        VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT | // 校验消息
        VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT; // 性能消息
    createInfo.pfnUserCallback = debugCallback; // 回调函数
    createInfo.pUserData = nullptr; // 用户自定义数据
}
}