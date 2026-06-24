#include "Device.h"

#include <iostream>
#include <set>
#include <stdexcept>
#include <vector>

namespace
{
const std::vector<const char*> g_DeviceExtensions = {
    VK_KHR_SWAPCHAIN_EXTENSION_NAME // VK_KHR_SWAPCHAIN_EXTENSION_NAME 是一个宏，定义为 "VK_KHR_swapchain"
};
}

namespace vkp
{
Device::Device(VkPhysicalDevice physicalDevice, const QueueFamilyIndices& indices)
    : m_PhysicalDevice(physicalDevice), m_QueueFamilyIndices(indices)
{
    createLogicalDevice();
}

Device::~Device()
{
    if(m_Device != VK_NULL_HANDLE){
        vkDestroyDevice(m_Device, nullptr); // 销毁逻辑设备
    }
}

Device::operator VkDevice() const
{
    return m_Device;
}

VkDevice Device::GetHandle() const
{
    return m_Device;
}

VkQueue Device::GetGraphicsQueue() const
{
    return m_GraphicsQueue;
}

VkQueue Device::GetPresentQueue() const
{
    return m_PresentQueue;
}

uint32_t Device::GetGraphicsQueueFamily() const
{
    return m_QueueFamilyIndices.graphicsFamily.value();
}

uint32_t Device::GetPresentQueueFamily() const
{
    return m_QueueFamilyIndices.presentFamily.value();
}

void Device::createLogicalDevice(){ // 创建逻辑设备
    QueueFamilyIndices indices = m_QueueFamilyIndices; // 查找队列族索引

    std::vector<VkDeviceQueueCreateInfo> queueCreateInfos; // 队列创建信息数组
    std::set<uint32_t> uniqueQueueFamilies = {
        indices.graphicsFamily.value(),
        indices.presentFamily.value()
    }; // 唯一的队列族索引

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
    createInfo.enabledLayerCount = 0; // device layer 已废弃，validation layer 只在 instance 开启

    if(vkCreateDevice(m_PhysicalDevice, &createInfo, nullptr, &m_Device) != VK_SUCCESS){ // 创建设备
        throw std::runtime_error("Failed to create logical device!");
    }

    std::cout<<"Logical device created: OK\n";

    vkGetDeviceQueue(m_Device, indices.graphicsFamily.value(), 0, &m_GraphicsQueue); // 获取图形队列
    std::cout<<"Graphics queue: OK\n";

    vkGetDeviceQueue(m_Device, indices.presentFamily.value(), 0, &m_PresentQueue); // 获取呈现队列
    std::cout<<"Present queue: OK\n";
}

uint32_t Device::FindMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties) const
{ // 查找内存类型
    VkPhysicalDeviceMemoryProperties memProperties{};
    vkGetPhysicalDeviceMemoryProperties(m_PhysicalDevice, &memProperties);

    for(uint32_t i = 0; i < memProperties.memoryTypeCount; i++){
        if((typeFilter & (1 << i)) &&
           (memProperties.memoryTypes[i].propertyFlags & properties) == properties){
            return i;
        }
    }

    throw std::runtime_error("Failed to find suitable memory type!");
}

VkDeviceSize Device::GetUniformBufferAlignment(VkDeviceSize instanceSize) const
{ // 获取统一缓冲区对齐
    VkPhysicalDeviceProperties properties{};
    vkGetPhysicalDeviceProperties(m_PhysicalDevice, &properties);

    VkDeviceSize alignment = properties.limits.minUniformBufferOffsetAlignment;
    if(alignment > 0){
        return (instanceSize + alignment - 1) & ~(alignment - 1);
    }

    return instanceSize;
}

VkDeviceSize Device::GetNonCoherentAtomSizeAlignment(VkDeviceSize instanceSize) const
{ // 获取非一致原子大小对齐
    VkPhysicalDeviceProperties properties{};
    vkGetPhysicalDeviceProperties(m_PhysicalDevice, &properties);

    VkDeviceSize alignment = properties.limits.nonCoherentAtomSize;
    if(alignment > 0){
        return (instanceSize + alignment - 1) & ~(alignment - 1);
    }

    return instanceSize;
}
}