#include "scene/water/render/ComputeImage2D.h"

#include <stdexcept>

namespace water
{
ComputeImage2D::ComputeImage2D(
    VkPhysicalDevice physicalDevice,
    VkDevice device,
    uint32_t width,
    uint32_t height,
    VkFormat format,
    VkImageUsageFlags usage
)
    : m_PhysicalDevice(physicalDevice),
      m_Device(device),
      m_Width(width),
      m_Height(height),
      m_Format(format),
      m_Usage(usage)
{
    CreateImage();
    CreateImageView();
}

ComputeImage2D::~ComputeImage2D()
{
    if(m_ImageView != VK_NULL_HANDLE){
        vkDestroyImageView(m_Device, m_ImageView, nullptr);
    }

    if(m_Image != VK_NULL_HANDLE){
        vkDestroyImage(m_Device, m_Image, nullptr);
    }

    if(m_Memory != VK_NULL_HANDLE){
        vkFreeMemory(m_Device, m_Memory, nullptr);
    }
}

VkDescriptorImageInfo ComputeImage2D::GetStorageDescriptorInfo() const
{
    VkDescriptorImageInfo info{};
    info.imageView = m_ImageView;
    info.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
    return info;
}

VkDescriptorImageInfo ComputeImage2D::GetSampledDescriptorInfo(VkSampler sampler) const
{
    VkDescriptorImageInfo info{};
    info.sampler = sampler;
    info.imageView = m_ImageView;
    info.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
    return info;
}

VkFormat ComputeImage2D::GetFormat() const
{
    return m_Format;
}

uint32_t ComputeImage2D::GetWidth() const
{
    return m_Width;
}

uint32_t ComputeImage2D::GetHeight() const
{
    return m_Height;
}

uint32_t ComputeImage2D::FindMemoryType(
    uint32_t typeFilter,
    VkMemoryPropertyFlags properties
) const
{
    VkPhysicalDeviceMemoryProperties memoryProperties{};
    vkGetPhysicalDeviceMemoryProperties(m_PhysicalDevice, &memoryProperties);

    for(uint32_t i = 0; i < memoryProperties.memoryTypeCount; i++){
        if((typeFilter & (1 << i)) &&
            (memoryProperties.memoryTypes[i].propertyFlags & properties) == properties){
            return i;
        }
    }

    throw std::runtime_error("ComputeImage2D failed to find memory type");
}

void ComputeImage2D::CreateImage()
{
    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.extent.width = m_Width;
    imageInfo.extent.height = m_Height;
    imageInfo.extent.depth = 1;
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.format = m_Format;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imageInfo.usage = m_Usage;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    if(vkCreateImage(m_Device, &imageInfo, nullptr, &m_Image) != VK_SUCCESS){
        throw std::runtime_error("Failed to create ComputeImage2D image");
    }

    VkMemoryRequirements memoryRequirements{};
    vkGetImageMemoryRequirements(m_Device, m_Image, &memoryRequirements);

    VkMemoryAllocateInfo allocateInfo{};
    allocateInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocateInfo.allocationSize = memoryRequirements.size;
    allocateInfo.memoryTypeIndex = FindMemoryType(
        memoryRequirements.memoryTypeBits,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT
    );

    if(vkAllocateMemory(m_Device, &allocateInfo, nullptr, &m_Memory) != VK_SUCCESS){
        throw std::runtime_error("Failed to allocate ComputeImage2D memory");
    }

    vkBindImageMemory(m_Device, m_Image, m_Memory, 0);
}

void ComputeImage2D::CreateImageView()
{
    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = m_Image;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = m_Format;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 1;

    if(vkCreateImageView(m_Device, &viewInfo, nullptr, &m_ImageView) != VK_SUCCESS){
        throw std::runtime_error("Failed to create ComputeImage2D image view");
    }
}

// 把图像从刚创建时的 VK_IMAGE_LAYOUT_UNDEFINED（未定义）布局，强制转换到 VK_IMAGE_LAYOUT_GENERAL（通用）布局
// 新创建的 VkImage 处于未定义状态，GPU 无法直接安全使用。泡沫状态图像需要被计算着色器读写，必须通过这个屏障来激活并完成初始化
void ComputeImage2D::RecordTransitionToGeneral(VkCommandBuffer commandBuffer)
{
    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    barrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = m_Image;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;
    barrier.srcAccessMask = 0;
    barrier.dstAccessMask =
        VK_ACCESS_SHADER_READ_BIT |
        VK_ACCESS_SHADER_WRITE_BIT;

    vkCmdPipelineBarrier(
        commandBuffer,
        VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        0,
        0,
        nullptr,
        0,
        nullptr,
        1,
        &barrier
    );
}

// 在图像处于 VK_IMAGE_LAYOUT_GENERAL 布局时，调用 vkCmdClearColorImage 将整张图像的颜色值重置（例如清为 0.0）
// 泡沫的初始状态应为“没有泡沫”（全黑），如果没有清零就直接读取，会出现未定义的随机像素或脏数据
void ComputeImage2D::RecordClear(VkCommandBuffer commandBuffer, float value)
{
    VkClearColorValue clearValue{};
    clearValue.float32[0] = value;
    clearValue.float32[1] = value;
    clearValue.float32[2] = value;
    clearValue.float32[3] = value;

    VkImageSubresourceRange range{};
    range.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    range.baseMipLevel = 0;
    range.levelCount = 1;
    range.baseArrayLayer = 0;
    range.layerCount = 1;

    vkCmdClearColorImage(
        commandBuffer,
        m_Image,
        VK_IMAGE_LAYOUT_GENERAL,
        &clearValue,
        1,
        &range
    );
}

// 当上一个计算着色器（如泡沫源合成）写完了图像，而下一个计算着色器（如泡沫平流）准备读取它时，插入这个屏障
// 对应你 Ping-Pong 流程中的 Source Pass 和 Advect Pass。它确保第一个计算着色器所有写入操作对第二个计算着色器完全可见，防止读到半成品
void ComputeImage2D::RecordComputeWriteToComputeReadBarrier(VkCommandBuffer commandBuffer)
{
    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = m_Image;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.layerCount = 1;
    barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

    vkCmdPipelineBarrier(
        commandBuffer,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        0,
        0,
        nullptr,
        0,
        nullptr,
        1,
        &barrier
    );
}

// 当最新的泡沫状态帧被计算着色器更新完毕，而片段着色器准备将其采样并绘制到屏幕上时，插入这个屏障
// 这是整个泡沫系统与水面渲染的连接点。它确保计算写入的数据完全刷新到显存，片段着色器采样时不会拿到撕裂或不完整的泡沫状态
void ComputeImage2D::RecordComputeWriteToFragmentReadBarrier(VkCommandBuffer commandBuffer)
{
    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = m_Image;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.layerCount = 1;
    barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

    vkCmdPipelineBarrier(
        commandBuffer,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
        0,
        0,
        nullptr,
        0,
        nullptr,
        1,
        &barrier
    );
}

// 当上一帧的泡沫状态还在被片段着色器读取时，新的计算着色器准备向该图像写入新一帧的数据，插入这个屏障
// 在 Ping-Pong 交替时，上一帧的输出图像（现在被片段着色器读取）将成为下一帧的输入图像（将被计算着色器覆盖）。
// 这个屏障确保片段着色器读取完当前帧的画面后，计算着色器才能安全地开始写入下一帧的泡沫数据
void ComputeImage2D::RecordFragmentReadToComputeWriteBarrier(VkCommandBuffer commandBuffer)
{
    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = m_Image;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.layerCount = 1;
    barrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT;

    vkCmdPipelineBarrier(
        commandBuffer,
        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        0,
        0,
        nullptr,
        0,
        nullptr,
        1,
        &barrier
    );
}

void ComputeImage2D::RecordComputeReadToComputeWriteBarrier(VkCommandBuffer commandBuffer)
{
    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = m_Image;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.layerCount = 1;
    barrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT;

    vkCmdPipelineBarrier(
        commandBuffer,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        0,
        0,
        nullptr,
        0,
        nullptr,
        1,
        &barrier
    );
}
}
