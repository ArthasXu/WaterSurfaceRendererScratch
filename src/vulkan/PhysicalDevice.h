#pragma once

#include <vulkan/vulkan.h>

#include <optional>
#include <vector>

namespace vkp
{
struct QueueFamilyIndices{                      // 用于存储队列族索引
    std::optional<uint32_t> graphicsFamily;     // 队列族索引，用于图形命令
    std::optional<uint32_t> presentFamily;      // 队列族索引，用于呈现命令

    bool isComplete() const
    {
        return graphicsFamily.has_value() && presentFamily.has_value();
    }
};

struct SwapChainSupportDetails{                 // 用于存储交换链支持信息, 负责管理呈现到屏幕的图像缓冲区序列 
    VkSurfaceCapabilitiesKHR capabilities;      // 交换链的能力, 如最小和最大图像数量、支持的图像格式和大小等
    std::vector<VkSurfaceFormatKHR> formats;    // 支持的图像格式, 如颜色深度和通道数
    std::vector<VkPresentModeKHR> presentModes; // 支持的呈现模式, 如立即呈现、双缓冲等
};

class PhysicalDevice
{
public:
    PhysicalDevice(VkInstance instance, VkSurfaceKHR surface);

    PhysicalDevice(const PhysicalDevice&) = delete;
    PhysicalDevice& operator=(const PhysicalDevice&) = delete;

    operator VkPhysicalDevice() const;
    VkPhysicalDevice GetHandle() const;
    const QueueFamilyIndices& GetQueueFamilyIndices() const;
    SwapChainSupportDetails QuerySwapChainSupport() const;

private:
    void pickPhysicalDevice();
    bool isDeviceSuitable(VkPhysicalDevice device) const;
    QueueFamilyIndices findQueueFamilies(VkPhysicalDevice device) const;
    bool checkDeviceExtensionSupport(VkPhysicalDevice device) const;
    SwapChainSupportDetails querySwapChainSupport(VkPhysicalDevice device) const;
    int rateDevice(VkPhysicalDevice device) const;

private:
    VkInstance m_Instance = VK_NULL_HANDLE;                 // Vulkan 实例句柄
    VkSurfaceKHR m_Surface = VK_NULL_HANDLE;                // 窗口表面句柄
    VkPhysicalDevice m_PhysicalDevice = VK_NULL_HANDLE;     // 物理设备句柄
    QueueFamilyIndices m_QueueFamilyIndices;                // 队列族索引
};
}