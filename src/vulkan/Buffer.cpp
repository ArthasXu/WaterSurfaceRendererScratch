#include "Buffer.h"

#include <cstring>
#include <stdexcept>

namespace vkp
{
Buffer::Buffer(
    VkPhysicalDevice physicalDevice,
    VkDevice device,
    VkDeviceSize size,
    VkBufferUsageFlags usage,
    VkMemoryPropertyFlags properties
)
    : m_PhysicalDevice(physicalDevice),
      m_Device(device),
      m_Size(size)
{
    createBuffer(size, usage, properties); // 创建缓冲区
}

Buffer::~Buffer(){
    Unmap(); // 取消映射缓冲区

    if(m_Buffer != VK_NULL_HANDLE){
        vkDestroyBuffer(m_Device, m_Buffer, nullptr); // 销毁缓冲区
    }

    if(m_Memory != VK_NULL_HANDLE){
        vkFreeMemory(m_Device, m_Memory, nullptr); // 释放内存
    }
}

Buffer::operator VkBuffer() const{
    return m_Buffer;
}

VkBuffer Buffer::GetHandle() const{
    return m_Buffer;
}

void Buffer::Map(){ // 映射缓冲区
    if(m_Mapped != nullptr){
        return;
    }

    // VkResult vkMapMemory(
    //     VkDevice                                    device,  // 设备
    //     VkDeviceMemory                              memory,  // 内存
    //     VkDeviceSize                                offset,  // 偏移量
    //     VkDeviceSize                                size,    // 大小
    //     VkMemoryMapFlags                            flags,   // 标志
    //     void**                                      ppData   // 数据指针
    // );
    if(vkMapMemory(m_Device, m_Memory, 0, m_Size, 0, &m_Mapped) != VK_SUCCESS){
        throw std::runtime_error("Failed to map buffer!");
    }
}

void Buffer::Unmap(){ // 取消映射缓冲区
    if(m_Mapped != nullptr){
        vkUnmapMemory(m_Device, m_Memory); // 取消映射内存
        m_Mapped = nullptr;
    }
}

void Buffer::CopyToMapped(const void* data, VkDeviceSize size, VkDeviceSize offset){ // 将数据复制到映射的缓冲区
    if(m_Mapped == nullptr){
        throw std::runtime_error("Failed to copy to unmapped buffer!");
    }

    if(offset + size > m_Size){
        throw std::runtime_error("Failed to copy to buffer with offset and size out of bounds!");   
    }

    // void* memcpy( void* dest, const void* src, std::size_t count );
    std::memcpy(static_cast<char*>(m_Mapped) + offset, data, static_cast<size_t>(size)); // 复制数据
}

void Buffer::FlushMappedRange(VkDeviceSize size, VkDeviceSize offset){ // 刷新映射的缓冲区范围
    VkMappedMemoryRange mappedRange{};
    mappedRange.sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE; // 结构类型
    mappedRange.memory = m_Memory; // 内存
    mappedRange.offset = offset; // 偏移量
    mappedRange.size = size; // 大小

    vkFlushMappedMemoryRanges(m_Device, 1, &mappedRange); // 刷新映射的内存范围
}

uint32_t Buffer::FindMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties) const{ // 查找内存类型
    VkPhysicalDeviceMemoryProperties memoryProperties{}; // 内存属性
    vkGetPhysicalDeviceMemoryProperties(m_PhysicalDevice, &memoryProperties); // 获取物理设备内存属性

    for(uint32_t i = 0; i < memoryProperties.memoryTypeCount; ++i){
        if((typeFilter & (1 << i)) && // 类型过滤器
            (memoryProperties.memoryTypes[i].propertyFlags & properties) == properties){ // 内存属性
            return i;
        }
    }

    throw std::runtime_error("Failed to find suitable memory type!");
}

void Buffer::createBuffer(VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties){ // 创建缓冲区
    VkBufferCreateInfo bufferInfo{}; // 缓冲区信息
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO; // 结构类型
    bufferInfo.size = size; // 大小
    bufferInfo.usage = usage; // 用途
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE; // 共享模式
    
    if(vkCreateBuffer(m_Device, &bufferInfo, nullptr, &m_Buffer) != VK_SUCCESS){ // 创建缓冲区
        throw std::runtime_error("Failed to create buffer!");
    }

    VkMemoryRequirements memRequirements{}; // 内存需求
    vkGetBufferMemoryRequirements(m_Device, m_Buffer, &memRequirements); // 获取缓冲区内存需求

    VkMemoryAllocateInfo allocInfo{}; // 内存分配信息
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO; // 结构类型
    allocInfo.allocationSize = memRequirements.size; // 分配大小
    allocInfo.memoryTypeIndex = FindMemoryType(memRequirements.memoryTypeBits, properties); // 内存类型索引

    if(vkAllocateMemory(m_Device, &allocInfo, nullptr, &m_Memory) != VK_SUCCESS){ // 分配内存
        throw std::runtime_error("Failed to allocate buffer memory!");
    }

    vkBindBufferMemory(m_Device, m_Buffer, m_Memory, 0); // 绑定缓冲区内存
}


}