#pragma once

#include "vulkan/Image.h"
#include "vulkan/ImageView.h"

#include <vulkan/vulkan.h>

#include <memory>

namespace water
{
class DepthBuffer
{
public:
    DepthBuffer(
        VkPhysicalDevice physicalDevice,
        VkDevice device,
        VkExtent2D extent,
        VkFormat format
    );

    ~DepthBuffer() = default;

    DepthBuffer(const DepthBuffer&) = delete;
    DepthBuffer& operator=(const DepthBuffer&) = delete;

    VkImageView GetImageView() const;
    VkFormat GetFormat() const;

private:
    VkDevice m_Device = VK_NULL_HANDLE;
    VkFormat m_Format = VK_FORMAT_UNDEFINED;

    std::unique_ptr<vkp::Image> m_Image;
    std::unique_ptr<vkp::ImageView> m_ImageView;
};
}