#include "ImageView.h"

#include <stdexcept>

namespace vkp
{
ImageView::ImageView(VkDevice device, VkImage image, VkFormat format)
    : m_Device(device)
{
    createImageView(image, format);
}

ImageView::~ImageView()
{
    if(m_ImageView != VK_NULL_HANDLE){
        vkDestroyImageView(m_Device, m_ImageView, nullptr); // 销毁图像视图
    }
}

ImageView::operator VkImageView() const
{
    return m_ImageView;
}

VkImageView ImageView::GetHandle() const
{
    return m_ImageView;
}

void ImageView::createImageView(VkImage image, VkFormat format){ // 创建图像视图
    VkImageViewCreateInfo createInfo{}; // 图像视图创建信息
    createInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO; // 结构体类型
    createInfo.image = image; // 图像
    createInfo.viewType = VK_IMAGE_VIEW_TYPE_2D; // 图像视图类型
    createInfo.format = format; // 图像格式

    createInfo.components.r = VK_COMPONENT_SWIZZLE_IDENTITY; // 红色分量
    createInfo.components.g = VK_COMPONENT_SWIZZLE_IDENTITY; // 绿色分量
    createInfo.components.b = VK_COMPONENT_SWIZZLE_IDENTITY; // 蓝色分量
    createInfo.components.a = VK_COMPONENT_SWIZZLE_IDENTITY; // 透明度分量

    createInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT; // 图像视图范围
    createInfo.subresourceRange.baseMipLevel = 0; // 基础 mipmap 级别
    createInfo.subresourceRange.levelCount = 1; // mipmap 级别数量
    createInfo.subresourceRange.baseArrayLayer = 0; // 基础数组层
    createInfo.subresourceRange.layerCount = 1; // 数组层数量

    if(vkCreateImageView(m_Device, &createInfo, nullptr, &m_ImageView) != VK_SUCCESS){ // 创建图像视图
        throw std::runtime_error("Failed to create image views!"); // 失败
    }
}
}