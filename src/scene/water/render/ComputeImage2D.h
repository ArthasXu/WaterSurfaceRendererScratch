#pragma once

#include <vulkan/vulkan.h>

#include <cstdint>

namespace water
{
class ComputeImage2D
{
public:
    ComputeImage2D(
        VkPhysicalDevice physicalDevice,
        VkDevice device,
        uint32_t width,
        uint32_t height,
        VkFormat format,
        VkImageUsageFlags usage
    );

    ~ComputeImage2D();

    ComputeImage2D(const ComputeImage2D&) = delete;
    ComputeImage2D& operator=(const ComputeImage2D&) = delete;

    VkDescriptorImageInfo GetStorageDescriptorInfo() const;
    VkDescriptorImageInfo GetSampledDescriptorInfo(VkSampler sampler) const;

    void RecordTransitionToGeneral(VkCommandBuffer commandBuffer);
    void RecordClear(VkCommandBuffer commandBuffer, float value);
    void RecordComputeWriteToComputeReadBarrier(VkCommandBuffer commandBuffer);
    void RecordComputeWriteToFragmentReadBarrier(VkCommandBuffer commandBuffer);
    void RecordFragmentReadToComputeWriteBarrier(VkCommandBuffer commandBuffer);

    VkFormat GetFormat() const;
    uint32_t GetWidth() const;
    uint32_t GetHeight() const;

private:
    uint32_t FindMemoryType(
        uint32_t typeFilter,
        VkMemoryPropertyFlags properties
    ) const;

    void CreateImage();
    void CreateImageView();

private:
    VkPhysicalDevice m_PhysicalDevice = VK_NULL_HANDLE;
    VkDevice m_Device = VK_NULL_HANDLE;

    uint32_t m_Width = 0;
    uint32_t m_Height = 0;
    VkFormat m_Format = VK_FORMAT_UNDEFINED;
    VkImageUsageFlags m_Usage = 0;

    VkImage m_Image = VK_NULL_HANDLE;
    VkDeviceMemory m_Memory = VK_NULL_HANDLE;
    VkImageView m_ImageView = VK_NULL_HANDLE;
};
}