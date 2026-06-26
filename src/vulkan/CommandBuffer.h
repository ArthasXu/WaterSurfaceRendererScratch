#pragma once

#include <vulkan/vulkan.h>

namespace vkp
{
class CommandBuffer
{
public:
    CommandBuffer(VkDevice device, VkCommandPool commandPool);
    ~CommandBuffer();

    CommandBuffer(const CommandBuffer&) = delete; // 禁止拷贝构造函数
    CommandBuffer& operator=(const CommandBuffer&) = delete; // 禁止赋值运算符

    operator VkCommandBuffer() const;
    VkCommandBuffer GetHandle() const;

    void Reset();
    void Begin();
    void End();

private:
    VkDevice m_Device = VK_NULL_HANDLE; // 逻辑设备句柄, 用于创建命令缓冲区
    VkCommandPool m_CommandPool = VK_NULL_HANDLE; // 命令池句柄, 用于分配命令缓冲区
    VkCommandBuffer m_CommandBuffer = VK_NULL_HANDLE; // 命令缓冲区句柄, 用于记录命令
};
}