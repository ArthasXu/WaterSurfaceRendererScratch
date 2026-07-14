#include "PhysicalDevice.h"

#include <iostream>
#include <set>
#include <stdexcept>

namespace
{
const std::vector<const char*> DeviceExtensions = {
    VK_KHR_SWAPCHAIN_EXTENSION_NAME
}; // 设备扩展
}

namespace vkp
{
PhysicalDevice::PhysicalDevice(VkInstance instance, VkSurfaceKHR surface)
    : m_Instance(instance), m_Surface(surface)
{
    pickPhysicalDevice();
}

PhysicalDevice::operator VkPhysicalDevice() const{
    return m_PhysicalDevice;
}

VkPhysicalDevice PhysicalDevice::GetHandle() const{
    return m_PhysicalDevice;
}

const QueueFamilyIndices& PhysicalDevice::GetQueueFamilyIndices() const{
    return m_QueueFamilyIndices;
}

bool PhysicalDevice::GraphicsQueueSupportsCompute() const
{
    return m_QueueFamilyIndices.graphicsFamily.has_value() &&
           m_QueueFamilyIndices.computeFamily.has_value() &&
           m_QueueFamilyIndices.graphicsFamily.value() ==
               m_QueueFamilyIndices.computeFamily.value();
}

SwapChainSupportDetails PhysicalDevice::QuerySwapChainSupport() const{
    return querySwapChainSupport(m_PhysicalDevice);
}

void PhysicalDevice::pickPhysicalDevice(){ // 选择物理设备
    uint32_t deviceCount = 0; // 设备数量
    vkEnumeratePhysicalDevices(m_Instance, &deviceCount, nullptr); // 获取设备数量

    if(deviceCount == 0){ // 如果设备数量为 0
        throw std::runtime_error("Failed to find GPUs with Vulkan support!"); // 抛出异常
    }

    std::vector<VkPhysicalDevice> devices(deviceCount); // 设备数组
    vkEnumeratePhysicalDevices(m_Instance, &deviceCount, devices.data()); // 获取设备数组
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

    m_PhysicalDevice = bestDevice; // 更新物理设备句柄

    VkPhysicalDeviceProperties selectedProperties{}; // 设备属性
    vkGetPhysicalDeviceProperties(m_PhysicalDevice, &selectedProperties); // 获取设备属性

    m_QueueFamilyIndices = findQueueFamilies(m_PhysicalDevice); // 查找队列族索引

    std::cout << "\nSelected GPU:\n";
    std::cout << "  " << selectedProperties.deviceName << "\n\n";
    std::cout << "Graphics queue family: " << m_QueueFamilyIndices.graphicsFamily.value() << "\n";
    std::cout << "Present queue family: " << m_QueueFamilyIndices.presentFamily.value() << "\n";
    std::cout << "Compute queue family: " << m_QueueFamilyIndices.computeFamily.value() << "\n";
    std::cout << "Graphics queue supports compute: "
        << (GraphicsQueueSupportsCompute() ? "YES" : "NO")
        << "\n";
    std::cout << "Swapchain support: OK\n";
}

QueueFamilyIndices PhysicalDevice::findQueueFamilies(VkPhysicalDevice device) const { // 为指定的物理设备查找所需的队列族索引（图形、呈现），并返回一个包含索引的结构体
    QueueFamilyIndices indices; // 队列族索引
    uint32_t queueFamilyCount = 0; // 队列族数量
    vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, nullptr); // 获取队列族数量
    std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount); // 队列族属性数组
    vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, queueFamilies.data()); // 获取队列族属性

    for(uint32_t i = 0; i < queueFamilyCount; ++i){ // 遍历队列族
        if(queueFamilies[i].queueFlags & VK_QUEUE_GRAPHICS_BIT){ // 如果队列族支持图形
            indices.graphicsFamily = i; // 记录图形队列族索引

            if(queueFamilies[i].queueFlags & VK_QUEUE_COMPUTE_BIT){
                indices.computeFamily = i; // 记录计算队列族索引
            }
        }
        VkBool32 presentSupport = false; // 记录是否支持呈现
        vkGetPhysicalDeviceSurfaceSupportKHR(device, i, m_Surface, &presentSupport); // 获取是否支持呈现
        if(presentSupport){ // 如果支持呈现
            indices.presentFamily = i; // 记录呈现队列族索引
        }

        if(!indices.computeFamily.has_value() &&
            (queueFamilies[i].queueFlags & VK_QUEUE_COMPUTE_BIT)){ // 如果计算队列族索引为空且队列族支持计算
            indices.computeFamily = i;
        }

        if(indices.isComplete()){ // 如果队列族索引完整
            break;
        }
    }

    return indices; // 返回队列族索引
}

bool PhysicalDevice::checkDeviceExtensionSupport(VkPhysicalDevice device) const { // 检查设备扩展支持
    uint32_t extensionCount = 0; // 扩展数量
    vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount, nullptr); // 获取扩展数量
    std::vector<VkExtensionProperties> availableExtensions(extensionCount); // 扩展属性数组
    vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount, availableExtensions.data()); // 获取扩展属性

    std::set<std::string> requiredExtensions(DeviceExtensions.begin(), DeviceExtensions.end()); // 所需扩展集合
    for(const auto& extension:availableExtensions){ // 遍历扩展
        requiredExtensions.erase(extension.extensionName); // 从所需扩展集合中删除已支持的扩展
    }

    return requiredExtensions.empty(); // 如果所需扩展集合为空，则表示所有扩展都已支持
}

SwapChainSupportDetails PhysicalDevice::querySwapChainSupport(VkPhysicalDevice device) const { // 查询交换链支持
    SwapChainSupportDetails details; // 交换链支持信息

    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(device, m_Surface, &details.capabilities); // 获取交换链能力
    
    uint32_t formatCount = 0; // 图像格式数量
    vkGetPhysicalDeviceSurfaceFormatsKHR(device, m_Surface, &formatCount, nullptr); // 获取图像格式数量
    if(formatCount != 0){ // 如果图像格式数量不为 0
        details.formats.resize(formatCount); // 调整图像格式数组大小    
        vkGetPhysicalDeviceSurfaceFormatsKHR(device, m_Surface, &formatCount, details.formats.data()); // 获取图像格式数组
    }    
    
    uint32_t presentModeCount = 0; // 呈现模式数量
    vkGetPhysicalDeviceSurfacePresentModesKHR(device, m_Surface, &presentModeCount, nullptr); // 获取呈现模式数量
    if(presentModeCount != 0){ // 如果呈现模式数量不为 0
        details.presentModes.resize(presentModeCount); // 调整呈现模式数组大小
        vkGetPhysicalDeviceSurfacePresentModesKHR(device, m_Surface, &presentModeCount, details.presentModes.data()); // 获取呈现模式数组
    }

    return details; // 返回交换链支持信息
}

bool PhysicalDevice::isDeviceSuitable(VkPhysicalDevice device) const { // 检查设备是否适合
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

int PhysicalDevice::rateDevice(VkPhysicalDevice device) const { // 评分设备, 独显优先，核显其次
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

}