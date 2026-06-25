#include "SyncObjects.h"

#include <stdexcept>

namespace vkp
{
FrameSyncObjects::FrameSyncObjects(VkDevice device)
    : m_Device(device)
{
    createSyncObjects();
}

FrameSyncObjects::~FrameSyncObjects()
{
    if(m_RenderFinished != VK_NULL_HANDLE){
        vkDestroySemaphore(m_Device, m_RenderFinished, nullptr);
    }

    if(m_ImageAvailable != VK_NULL_HANDLE){
        vkDestroySemaphore(m_Device, m_ImageAvailable, nullptr);
    }

    if(m_InFlight != VK_NULL_HANDLE){
        vkDestroyFence(m_Device, m_InFlight, nullptr);
    }
}

VkSemaphore FrameSyncObjects::ImageAvailable() const
{
    return m_ImageAvailable;
}

VkSemaphore FrameSyncObjects::RenderFinished() const
{
    return m_RenderFinished;
}

VkFence FrameSyncObjects::InFlightFence() const
{
    return m_InFlight;
}

void FrameSyncObjects::createSyncObjects(){ // 创建同步对象
    VkSemaphoreCreateInfo semaphoreInfo{}; // 信号量创建信息
    semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO; // 结构体类型

    VkFenceCreateInfo fenceInfo{}; // 栅栏创建信息
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO; // 结构体类型
    fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT; // 栅栏标志

    if(vkCreateSemaphore(m_Device, &semaphoreInfo, nullptr, &m_ImageAvailable) != VK_SUCCESS || // 创建图像可用信号量
       vkCreateSemaphore(m_Device, &semaphoreInfo, nullptr, &m_RenderFinished) != VK_SUCCESS || // 创建渲染完成信号量
       vkCreateFence(m_Device, &fenceInfo, nullptr, &m_InFlight) != VK_SUCCESS){ // 创建并发帧信号量
        throw std::runtime_error("Failed to create synchronization objects for a frame!"); // 失败
    }
}
}