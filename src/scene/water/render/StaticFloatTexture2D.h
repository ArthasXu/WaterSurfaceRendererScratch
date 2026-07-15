#pragma once

#include "vulkan/Buffer.h"
#include "vulkan/CommandPool.h"

#include <glm/glm.hpp>
#include <vulkan/vulkan.h>

#include <cstdint>
#include <vector>

// 一次性上传 1024x1 RGBA32F LUT，之后只读共享
namespace water
{
class StaticFloatTexture2D
{
public:
    StaticFloatTexture2D(
        VkPhysicalDevice physicalDevice,
        VkDevice device,
        vkp::CommandPool& commandPool,
        VkQueue queue,
        uint32_t width,
        uint32_t height,
        const std::vector<glm::vec4>& pixels
    );

    ~StaticFloatTexture2D();

    StaticFloatTexture2D(const StaticFloatTexture2D&) = delete;
    StaticFloatTexture2D& operator=(const StaticFloatTexture2D&) = delete;

    VkImage GetImage() const;
    VkImageView GetImageView() const;
    VkDescriptorImageInfo GetDescriptorInfo(VkSampler sampler) const;

private:
    uint32_t FindMemoryType(
        uint32_t typeFilter,
        VkMemoryPropertyFlags properties
    ) const;

    void CreateImage();
    void CreateImageView();
    void UploadPixels(
        vkp::CommandPool& commandPool,
        VkQueue queue,
        const std::vector<glm::vec4>& pixels
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

    VkFormat m_Format = VK_FORMAT_R32G32B32A32_SFLOAT;
    VkImage m_Image = VK_NULL_HANDLE;
    VkDeviceMemory m_Memory = VK_NULL_HANDLE;
    VkImageView m_ImageView = VK_NULL_HANDLE;
};
}