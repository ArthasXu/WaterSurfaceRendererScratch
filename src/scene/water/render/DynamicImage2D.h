#pragma once

#include <vulkan/vulkan.h>

#include <cstdint>

namespace water
{
class DynamicImage2D
{
public:
    // 服务于 Stage 6 的“静态网格 + 采样位移”方案
    DynamicImage2D(
        VkPhysicalDevice physicalDevice,
        VkDevice device,
        uint32_t width,
        uint32_t height,
        VkFormat format,
        VkImageUsageFlags usage
    );

    ~DynamicImage2D();

    DynamicImage2D(const DynamicImage2D&) = delete;
    DynamicImage2D& operator=(const DynamicImage2D&) = delete;

    VkImage GetImage() const;
    VkImageView GetImageView() const;
    VkFormat GetFormat() const;
    uint32_t GetWidth() const;
    uint32_t GetHeight() const;

    void RecordUpload(
        VkCommandBuffer commandBuffer,
        VkBuffer stagingBuffer
    );

    VkDescriptorImageInfo GetDescriptorInfo(VkSampler sampler) const;

private:
    // 设备本地内存 VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT GPU 专用显存，CPU 不能直接映射读写，带宽最高 用于静态网格、纹理等渲染资源
    // 主机可见内存 VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT CPU 可以通过映射（vkMapMemory）直接读写。通常与以下两个属性组合
    // 主机一致内存	HOST_VISIBLE | HOST_COHERENT CPU 写入后自动对 GPU 可见，无需手动 flush。常用于 uniform 缓冲、动态顶点缓冲
    // 主机缓存内存	HOST_VISIBLE | HOST_CACHED CPU 写入后需要手动 flush，才能对 GPU 可见。常用于 staging buffer
    // 懒分配内存	LAZILY_ALLOCATED	仅在部分 GPU（如 NVIDIA）上支持，用于延迟分配大块内存（如 MSAA 中间缓冲），驱动按需分配。
    uint32_t FindMemoryType(
        uint32_t typeFilter,
        VkMemoryPropertyFlags properties
    ) const;

    void CreateImage(VkImageUsageFlags usage);
    void CreateImageView();

    void RecordTransition(
        VkCommandBuffer commandBuffer,
        VkImageLayout oldLayout,
        VkImageLayout newLayout,
        VkPipelineStageFlags srcStage,
        VkAccessFlags srcAccess,
        VkPipelineStageFlags dstStage,
        VkAccessFlags dstAccess
    );

private:
    VkPhysicalDevice m_PhysicalDevice = VK_NULL_HANDLE;
    VkDevice m_Device = VK_NULL_HANDLE;

    uint32_t m_Width = 0;
    uint32_t m_Height = 0;
    VkFormat m_Format = VK_FORMAT_UNDEFINED;

    VkImage m_Image = VK_NULL_HANDLE;
    VkDeviceMemory m_Memory = VK_NULL_HANDLE;
    VkImageView m_ImageView = VK_NULL_HANDLE;

    VkImageLayout m_CurrentLayout = VK_IMAGE_LAYOUT_UNDEFINED;
};
}