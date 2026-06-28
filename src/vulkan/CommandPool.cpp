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

VkCommandBuffer CommandPool::BeginOneTimeCommands(VkDevice device){ // 开始一次性命令
    VkCommandBufferAllocateInfo allocInfo{}; // 命令缓冲区分配信息
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO; // 结构体类型
    allocInfo.commandPool = m_CommandPool; // 命令池
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY; // 命令缓冲区级别
    allocInfo.commandBufferCount = 1; // 命令缓冲区数量
    
    VkCommandBuffer commandBuffer = VK_NULL_HANDLE; // 命令缓冲区
    if(vkAllocateCommandBuffers(device, &allocInfo, &commandBuffer) != VK_SUCCESS){ // 分配命令缓冲区
        throw std::runtime_error("Failed to allocate command buffers!"); // 失败
    }

    VkCommandBufferBeginInfo beginInfo{}; // 命令缓冲区开始信息
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO; // 结构体类型
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT; // 命令缓冲区使用标志

    if(vkBeginCommandBuffer(commandBuffer, &beginInfo) != VK_SUCCESS){ // 开始命令缓冲区
        throw std::runtime_error("Failed to begin recording command buffer!"); // 失败
    }

    return commandBuffer;
}

void CommandPool::EndOneTimeCommands(VkDevice device, VkQueue queue, VkCommandBuffer commandBuffer){ // 结束一次性命令
    if(vkEndCommandBuffer(commandBuffer) != VK_SUCCESS){ // 结束命令缓冲区
        throw std::runtime_error("Failed to record command buffer!"); // 失败
    }

    VkSubmitInfo submitInfo{}; // 提交信息
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO; // 结构体类型
    submitInfo.commandBufferCount = 1; // 命令缓冲区数量
    submitInfo.pCommandBuffers = &commandBuffer; // 命令缓冲区数组

    if(vkQueueSubmit(queue, 1, &submitInfo, VK_NULL_HANDLE) != VK_SUCCESS){ // 提交命令缓冲区
        throw std::runtime_error("Failed to submit one time command buffer!"); // 失败
    }

    if(vkQueueWaitIdle(queue) != VK_SUCCESS){ // 等待队列空闲
        throw std::runtime_error("Failed to wait for queue idle!"); // 失败
    }

    vkFreeCommandBuffers(device, m_CommandPool, 1, &commandBuffer); // 释放命令缓冲区
}

void CommandPool::CopyBuffer(
    VkDevice device,
    VkQueue queue,
    VkBuffer srcBuffer,
    VkBuffer dstBuffer,
    VkDeviceSize size
){
    VkCommandBuffer commandBuffer = BeginOneTimeCommands(device); // 获取一个命令缓冲区

    // typedef struct VkBufferCopy {
    //     VkDeviceSize    srcOffset;
    //     VkDeviceSize    dstOffset;
    //     VkDeviceSize    size;
    // } VkBufferCopy; // 缓冲区复制区域
    VkBufferCopy copyRegion{}; // 缓冲区复制区域
    copyRegion.srcOffset = 0; // 源偏移量
    copyRegion.dstOffset = 0; // 目标偏移量
    copyRegion.size = size; // 复制大小

    vkCmdCopyBuffer(commandBuffer, srcBuffer, dstBuffer, 1, &copyRegion); // 复制缓冲区

    EndOneTimeCommands(device, queue, commandBuffer); // 结束命令缓冲区
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