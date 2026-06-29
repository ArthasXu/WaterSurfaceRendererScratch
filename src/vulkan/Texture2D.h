#pragma once

#include "vulkan/Image.h"
#include "vulkan/ImageView.h"
#include "vulkan/Sampler.h"

#include <vulkan/vulkan.h>

#include <memory>
#include <string>

namespace vkp
{
class Texture2D
{
public:
    Texture2D(
        VkPhysicalDevice physicalDevice,
        VkDevice device,
        VkCommandPool commandPool,
        VkQueue graphicsQueue,
        const std::string& path
    ); // 从文件加载纹理
    ~Texture2D() = default; // 析构函数

    Texture2D(const Texture2D&) = delete;
    Texture2D& operator=(const Texture2D&) = delete;

    const VkDescriptorImageInfo& GetDescriptorInfo() const; // 获取描述符信息
    VkImageView GetImageView() const; // 获取图像视图
    VkSampler GetSampler() const; // 获取采样器

private:
    std::unique_ptr<Image> m_Image; // 图像
    std::unique_ptr<ImageView> m_ImageView; // 图像视图
    std::unique_ptr<Sampler> m_Sampler; // 采样器
    VkDescriptorImageInfo m_DescriptorInfo{}; // 描述符信息
};
}