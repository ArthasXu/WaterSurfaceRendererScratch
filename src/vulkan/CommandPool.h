#pragma once

#include <vulkan/vulkan.h>

namespace vkp
{
class CommandPool
{
public:
    CommandPool(VkDevice device, uint32_t graphicsQueueFamily);
    ~CommandPool();

    CommandPool(const CommandPool&) = delete;
    CommandPool& operator=(const CommandPool&) = delete;

    operator VkCommandPool() const;
    VkCommandPool GetHandle() const;

private:
    void createCommandPool(uint32_t graphicsQueueFamily); // 创建命令池

private:
    VkDevice m_Device = VK_NULL_HANDLE;                 // 逻辑设备句柄
    VkCommandPool m_CommandPool = VK_NULL_HANDLE;       // 命令池句柄
};
}