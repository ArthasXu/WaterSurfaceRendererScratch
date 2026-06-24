#pragma once

#include <vulkan/vulkan.h>

namespace vkp
{
class ImageView
{
public:
    ImageView(VkDevice device, VkImage image, VkFormat format);
    ~ImageView();

    ImageView(const ImageView&) = delete;
    ImageView& operator=(const ImageView&) = delete;

    operator VkImageView() const;
    VkImageView GetHandle() const;

private:
    void createImageView(VkImage image, VkFormat format); // 创建图像视图

private:
    VkDevice m_Device = VK_NULL_HANDLE;         // 逻辑设备句柄
    VkImageView m_ImageView = VK_NULL_HANDLE;   // 图像视图句柄
};
}