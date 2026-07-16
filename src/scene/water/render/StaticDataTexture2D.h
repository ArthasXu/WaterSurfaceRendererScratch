#pragma once

#include "vulkan/CommandPool.h"

#include <vulkan/vulkan.h>

#include <cstdint>

namespace water
{
class StaticDataTexture2D
{
public:
    StaticDataTexture2D(
        VkPhysicalDevice physicalDevice,
        VkDevice device,
        vkp::CommandPool& commandPool,
        VkQueue queue,
        uint32_t width,
        uint32_t height,
        VkFormat format,
        const void* data,
        VkDeviceSize dataSize
    );

    ~StaticDataTexture2D();

    StaticDataTexture2D(const StaticDataTexture2D&) = delete;
    StaticDataTexture2D& operator=(const StaticDataTexture2D&) = delete;

    VkDescriptorImageInfo GetDescriptorInfo(VkSampler sampler) const;
    VkFormat GetFormat() const;

private:
    uint32_t FindMemoryType(
        uint32_t typeFilter,
        VkMemoryPropertyFlags properties
    ) const;

    VkDeviceSize GetBytesPerTexel() const;

    void CreateImage();
    void CreateImageView();

    void UploadData(
        vkp::CommandPool& commandPool,
        VkQueue queue,
        const void* data,
        VkDeviceSize dataSize
    );

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
};
}