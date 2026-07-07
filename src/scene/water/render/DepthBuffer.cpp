#include "scene/water/render/DepthBuffer.h"

namespace water
{
DepthBuffer::DepthBuffer(
    VkPhysicalDevice physicalDevice,
    VkDevice device,
    VkExtent2D extent,
    VkFormat format
)
    : m_Device(device),
      m_Format(format)
{
    m_Image = std::make_unique<vkp::Image>(
        physicalDevice,
        device,
        extent.width,
        extent.height,
        format,
        VK_IMAGE_TILING_OPTIMAL,
        VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT
    );

    m_ImageView = std::make_unique<vkp::ImageView>(
        device,
        *m_Image,
        format,
        VK_IMAGE_ASPECT_DEPTH_BIT
    );
}

VkImageView DepthBuffer::GetImageView() const
{
    return *m_ImageView;
}

VkFormat DepthBuffer::GetFormat() const
{
    return m_Format;
}
}