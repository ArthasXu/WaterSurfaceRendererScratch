#include "vulkan/Texture2D.h"

#include "vulkan/Buffer.h"

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

#include <stdexcept>

namespace vkp
{
Texture2D::Texture2D(
    VkPhysicalDevice physicalDevice,
    VkDevice device,
    VkCommandPool commandPool,
    VkQueue graphicsQueue,
    const std::string& path
){ // 从文件加载纹理
    int texWidth = 0, texHeight = 0, texChannels = 0; // 纹理宽度、高度、通道数
    stbi_uc* pixels = stbi_load(path.c_str(), &texWidth, &texHeight, &texChannels, STBI_rgb_alpha); // 加载纹理
    if (!pixels) { // 如果加载失败
        throw std::runtime_error("failed to load texture image: " + path); // 抛出异常    
    }

    VkDeviceSize imageSize = static_cast<VkDeviceSize>(texWidth) *
        static_cast<VkDeviceSize>(texHeight) * 4; // 图像大小, 4 个字节一个像素
    
    Buffer stagingBuffer(
        physicalDevice,
        device,
        imageSize,
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
    );// 创建暂存缓冲区 HOST_VISIBLE | HOST_COHERENT 表示可以从CPU访问

    stagingBuffer.Map(); // 映射内存
    stagingBuffer.CopyToMapped(pixels, imageSize); // 复制数据，把顶点数据从 CPU 搬进了 host-visible 内存
    stagingBuffer.Unmap(); // 取消映射

    stbi_image_free(pixels); // 释放内存

    m_Image = std::make_unique<Image>(
        physicalDevice,
        device,
        static_cast<uint32_t>(texWidth),
        static_cast<uint32_t>(texHeight),
        VK_FORMAT_R8G8B8A8_SRGB,
        VK_IMAGE_TILING_OPTIMAL,
        VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT
    ); // 创建图像 DEVICE_LOCAL 表示只能从GPU访问 TRANSFER_DST_BIT 表示可以从CPU传输到GPU

    m_Image->TransitionLayout(
        commandPool,
        graphicsQueue,
        VK_IMAGE_LAYOUT_UNDEFINED,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL
    ); // 转换布局，从 UNDEFINED 转换到 TRANSFER_DST_OPTIMAL

    m_Image->CopyFromBuffer(
        commandPool,
        graphicsQueue,
        stagingBuffer
    ); // 从缓冲区拷贝数据到图像

    m_Image->TransitionLayout(
        commandPool,
        graphicsQueue,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
    ); // 转换布局，从 TRANSFER_DST_OPTIMAL 转换到 SHADER_READ_ONLY_OPTIMAL

    m_ImageView = std::make_unique<ImageView>(
        device,
        *m_Image,
        VK_FORMAT_R8G8B8A8_SRGB
    ); // 创建图像视图

    m_Sampler = std::make_unique<Sampler>(device); // 创建采样器
    
    m_DescriptorInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL; // 描述符信息
    m_DescriptorInfo.imageView = *m_ImageView; // 描述符信息
    m_DescriptorInfo.sampler = *m_Sampler; // 描述符信息
}

const VkDescriptorImageInfo& Texture2D::GetDescriptorInfo() const
{
    return m_DescriptorInfo;
}

VkImageView Texture2D::GetImageView() const
{
    return *m_ImageView;
}

VkSampler Texture2D::GetSampler() const
{
    return *m_Sampler;
}

}