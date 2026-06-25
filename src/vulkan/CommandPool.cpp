#include "CommandPool.h"

#include <iostream>
#include <stdexcept>

namespace vkp
{
CommandPool::CommandPool(VkDevice device, uint32_t graphicsQueueFamily)
    : m_Device(device)
{
    createCommandPool(graphicsQueueFamily);
}

CommandPool::~CommandPool()
{
    if(m_CommandPool != VK_NULL_HANDLE){
        vkDestroyCommandPool(m_Device, m_CommandPool, nullptr);
    }
}

CommandPool::operator VkCommandPool() const
{
    return m_CommandPool;
}

VkCommandPool CommandPool::GetHandle() const
{
    return m_CommandPool;
}

void CommandPool::createCommandPool(uint32_t graphicsQueueFamily){ // 创建命令池
    VkCommandPoolCreateInfo poolInfo{}; // 命令池创建信息
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO; // 结构体类型
    poolInfo.queueFamilyIndex = graphicsQueueFamily; // 队列族索引
    poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT; // 命令缓冲区重置标志

    if(vkCreateCommandPool(m_Device, &poolInfo, nullptr, &m_CommandPool) != VK_SUCCESS){ // 创建命令池
        throw std::runtime_error("Failed to create command pool!");
    }

    std::cout << "Command pool created: OK\n";
}
}