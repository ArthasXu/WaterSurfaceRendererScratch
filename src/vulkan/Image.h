#pragma once

#include <vulkan/vulkan.h>

namespace vkp
{
class Image
{
public:
    Image(
        VkPhysicalDevice physicalDevice,
        VkDevice device,
        uint32_t width,
        uint32_t height,
        VkFormat format,
        VkImageTiling tiling,
        VkImageUsageFlags usage,
        VkMemoryPropertyFlags properties
    );
    ~Image();

    Image(const Image&) = delete;
    Image& operator=(const Image&) = delete;

    operator VkImage() const; // 隐式转换为 VkImage
    VkImage GetHandle() const;
    VkFormat GetFormat() const;
    uint32_t GetWidth() const;
    uint32_t GetHeight() const;

    // 调用 vkCmdPipelineBarrier（通过一次性命令缓冲）将图像从一种布局（如 UNDEFINED、TRANSFER_DST_OPTIMAL）转换到另一种（如 SHADER_READ_ONLY_OPTIMAL）。
    // 这是 Vulkan 显式同步的核心：你必须告诉驱动图像当前处于什么状态，才能安全地读取或写入。
    // 例如，从缓冲区拷贝数据前，图像必须处于 TRANSFER_DST_OPTIMAL；着色器采样前，必须处于 SHADER_READ_ONLY_OPTIMAL
    void TransitionLayout(
        VkCommandPool commandPool,
        VkQueue queue,
        VkImageLayout oldLayout,
        VkImageLayout newLayout
    );

    void CopyFromBuffer(
        VkCommandPool commandPool,
        VkQueue queue,
        VkBuffer buffer
    ); // 从缓冲区拷贝数据到图像

private:
// 根据物理设备的内存类型，找到满足 VkMemoryPropertyFlags 要求的索引，用于分配设备内存    
uint32_t FindMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties) const; 
    void CreateImage(
        uint32_t width,
        uint32_t height,
        VkFormat format,
        VkImageTiling tiling,
        VkImageUsageFlags usage,
        VkMemoryPropertyFlags properties
    );

private:
    VkDevice m_Device = VK_NULL_HANDLE; // 逻辑设备句柄
    VkPhysicalDevice m_PhysicalDevice = VK_NULL_HANDLE; // 物理设备句柄
    VkImage m_Image = VK_NULL_HANDLE; // 图像句柄
    VkDeviceMemory m_Memory = VK_NULL_HANDLE; // 设备内存句柄
    VkFormat m_Format = VK_FORMAT_UNDEFINED; // 图像格式
    uint32_t m_Width = 0; // 图像宽度
    uint32_t m_Height = 0; // 图像高度
};
}