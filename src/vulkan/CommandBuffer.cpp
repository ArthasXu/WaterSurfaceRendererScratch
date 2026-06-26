#include "CommandBuffer.h"

#include <stdexcept> // std::runtime_error

namespace vkp
{
CommandBuffer::CommandBuffer(VkDevice device, VkCommandPool commandPool)
    : m_Device(device), m_CommandPool(commandPool){ // 构造函数, 初始化成员变量
        VkCommandBufferAllocateInfo allocInfo{}; // 命令缓冲区分配信息
        allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO; // 结构体类型
        allocInfo.commandPool = m_CommandPool; // 命令池
        allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY; // 命令缓冲区级别
        allocInfo.commandBufferCount = 1; // 命令缓冲区数量

        if(vkAllocateCommandBuffers(m_Device, &allocInfo, &m_CommandBuffer) != VK_SUCCESS){ // 分配命令缓冲区
            throw std::runtime_error("Failed to allocate command buffers!"); // 失败
        }
}

CommandBuffer::~CommandBuffer()
{
    if (m_CommandBuffer != VK_NULL_HANDLE) {
        vkFreeCommandBuffers(m_Device, m_CommandPool, 1, &m_CommandBuffer);
        m_CommandBuffer = VK_NULL_HANDLE;
    }
}

CommandBuffer::operator VkCommandBuffer() const
{
    return m_CommandBuffer;
}

VkCommandBuffer CommandBuffer::GetHandle() const
{
    return m_CommandBuffer;
}

void CommandBuffer::Reset()
{
    // VkResult vkResetCommandBuffer(
    //     VkCommandBuffer                             commandBuffer,   // 命令缓冲区
    //     VkCommandBufferResetFlags                   flags            // 重置标志, 可以是 VK_COMMAND_BUFFER_RESET_RELEASE_RESOURCES_BIT
    // );
    vkResetCommandBuffer(m_CommandBuffer, 0);
}

void CommandBuffer::Begin(){ // 开始记录命令
    VkCommandBufferBeginInfo beginInfo{}; // 命令缓冲区开始信息
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO; // 结构体类型

    if(vkBeginCommandBuffer(m_CommandBuffer, &beginInfo) != VK_SUCCESS){ // 开始记录命令
        throw std::runtime_error("Failed to begin recording command buffer!"); // 失败
    }
}

void CommandBuffer::End(){ // 结束记录命令
    if(vkEndCommandBuffer(m_CommandBuffer) != VK_SUCCESS){ // 结束记录命令
        throw std::runtime_error("Failed to record command buffer!"); // 失败
    }
}
}