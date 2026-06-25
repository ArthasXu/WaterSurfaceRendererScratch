#pragma once

#include <vulkan/vulkan.h>

namespace vkp
{
class FrameSyncObjects
{
public:
    explicit FrameSyncObjects(VkDevice device);
    ~FrameSyncObjects();

    FrameSyncObjects(const FrameSyncObjects&) = delete;
    FrameSyncObjects& operator=(const FrameSyncObjects&) = delete;

    VkSemaphore ImageAvailable() const; // 图像可用信号量, 用于同步图像的可用性
    VkSemaphore RenderFinished() const; // 渲染完成信号量, 用于同步渲染的完成
    VkFence InFlightFence() const; // 渲染完成栅栏, 用于同步渲染的完成

private:
    void createSyncObjects();

private:
    VkDevice m_Device = VK_NULL_HANDLE;             // 逻辑设备句柄, 用于创建同步对象
    VkSemaphore m_ImageAvailable = VK_NULL_HANDLE;  // 图像可用信号量, 用于同步图像的可用性
    VkSemaphore m_RenderFinished = VK_NULL_HANDLE;  // 渲染完成信号量, 用于同步渲染的完成
    VkFence m_InFlight = VK_NULL_HANDLE;            // 渲染完成栅栏, 用于同步渲染的完成
};
}