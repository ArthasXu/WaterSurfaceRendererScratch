#pragma once

#include "PhysicalDevice.h"

#include <vulkan/vulkan.h>

namespace vkp
{
class Device
{
public:
    Device(VkPhysicalDevice physicalDevice, const QueueFamilyIndices& indices);
    ~Device();

    Device(const Device&) = delete;
    Device& operator=(const Device&) = delete;

    operator VkDevice() const;
    VkDevice GetHandle() const;

    VkQueue GetGraphicsQueue() const;
    VkQueue GetPresentQueue() const;
    uint32_t GetGraphicsQueueFamily() const;
    uint32_t GetPresentQueueFamily() const;

    uint32_t FindMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties) const;
    VkDeviceSize GetUniformBufferAlignment(VkDeviceSize instanceSize) const;
    VkDeviceSize GetNonCoherentAtomSizeAlignment(VkDeviceSize instanceSize) const;

private:
    void createLogicalDevice(); // 创建逻辑设备

private:
    VkPhysicalDevice m_PhysicalDevice = VK_NULL_HANDLE;     // 物理设备句柄
    VkDevice m_Device = VK_NULL_HANDLE;                     // 逻辑设备句柄
    VkQueue m_GraphicsQueue = VK_NULL_HANDLE;               // 图形队列句柄
    VkQueue m_PresentQueue = VK_NULL_HANDLE;                // 呈现队列句柄
    QueueFamilyIndices m_QueueFamilyIndices;                // 队列族索引
};
}