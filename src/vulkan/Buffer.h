#pragma once

#include <vulkan/vulkan.h>

namespace vkp
{
class Buffer
{
public:
    Buffer(
        VkPhysicalDevice physicalDevice,    // 物理设备句柄
        VkDevice device,                    // 逻辑设备句柄
        VkDeviceSize size,                  // 缓冲区大小
        VkBufferUsageFlags usage,           // 缓冲区用途
        VkMemoryPropertyFlags properties    // 内存属性
    );
    ~Buffer();

    Buffer(const Buffer&) = delete;
    Buffer& operator=(const Buffer&) = delete;

    operator VkBuffer() const;              // 隐式转换为 VkBuffer
    VkBuffer GetHandle() const;             // 获取 VkBuffer 句柄

    void Map();                             // 映射缓冲区
    void Unmap();                           // 取消映射缓冲区
    void CopyToMapped(const void* data, VkDeviceSize size, VkDeviceSize offset = 0);    // 将数据复制到映射的缓冲区
    void FlushMappedRange(VkDeviceSize size, VkDeviceSize offset = 0);                  // 刷新映射的缓冲区范围

private:
    uint32_t FindMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties) const; // 查找内存类型
    void createBuffer(VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties); // 创建缓冲区

private:
    VkPhysicalDevice m_PhysicalDevice = VK_NULL_HANDLE;     // 物理设备句柄
    VkDevice m_Device = VK_NULL_HANDLE;                     // 逻辑设备句柄
    VkBuffer m_Buffer = VK_NULL_HANDLE;                     // 缓冲区句柄
    VkDeviceMemory m_Memory = VK_NULL_HANDLE;               // 内存句柄
    VkDeviceSize m_Size = 0;                                // 缓冲区大小
    void* m_Mapped = nullptr;                               // 映射的缓冲区指针
};
}