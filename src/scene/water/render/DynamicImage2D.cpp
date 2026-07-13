#include "scene/water/render/DynamicImage2D.h"

#include <stdexcept>

namespace water
{
DynamicImage2D::DynamicImage2D(
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
      m_Format(format)
{
    CreateImage(usage);
    CreateImageView();
}

DynamicImage2D::~DynamicImage2D()
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

VkImage DynamicImage2D::GetImage() const
{
    return m_Image;
}

VkImageView DynamicImage2D::GetImageView() const
{
    return m_ImageView;
}

VkFormat DynamicImage2D::GetFormat() const
{
    return m_Format;
}

uint32_t DynamicImage2D::GetWidth() const
{
    return m_Width;
}

uint32_t DynamicImage2D::GetHeight() const
{
    return m_Height;
}

uint32_t DynamicImage2D::FindMemoryType(
    uint32_t typeFilter,
    VkMemoryPropertyFlags properties
) const
{   // 查找内存类型
    VkPhysicalDeviceMemoryProperties memoryProperties{};
    vkGetPhysicalDeviceMemoryProperties(m_PhysicalDevice, &memoryProperties);

    for(uint32_t i = 0; i < memoryProperties.memoryTypeCount; i++){
        if((typeFilter & (1 << i)) &&
            (memoryProperties.memoryTypes[i].propertyFlags & properties) == properties){
            return i;
        }
    }

    throw std::runtime_error("DynamicImage2D failed to find suitable memory type");
}

void DynamicImage2D::CreateImage(VkImageUsageFlags usage)
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
    imageInfo.usage = usage;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    if(vkCreateImage(m_Device, &imageInfo, nullptr, &m_Image) != VK_SUCCESS){
        throw std::runtime_error("Failed to create DynamicImage2D image");
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
        throw std::runtime_error("Failed to allocate DynamicImage2D memory");
    }

    vkBindImageMemory(m_Device, m_Image, m_Memory, 0);
}

void DynamicImage2D::CreateImageView()
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
        throw std::runtime_error("Failed to create DynamicImage2D image view");
    }
}

void DynamicImage2D::RecordTransition(
    VkCommandBuffer commandBuffer,
    VkImageLayout oldLayout,
    VkImageLayout newLayout,
    VkPipelineStageFlags srcStage,
    VkAccessFlags srcAccess,
    VkPipelineStageFlags dstStage,
    VkAccessFlags dstAccess
)
{
    // 显式同步和布局转换
    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = oldLayout;
    barrier.newLayout = newLayout;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = m_Image;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;
    barrier.srcAccessMask = srcAccess;
    barrier.dstAccessMask = dstAccess;

    vkCmdPipelineBarrier(
        commandBuffer,
        srcStage,
        dstStage,
        0,
        0,
        nullptr,
        0,
        nullptr,
        1,
        &barrier
    );

    m_CurrentLayout = newLayout;
}

void DynamicImage2D::RecordUpload(
    VkCommandBuffer commandBuffer,
    VkBuffer stagingBuffer
)
{
    // 封装了“首次/后续 → 传输布局 → 拷贝数据 → 着色器可读布局”的完整流程，
    // 让你的位移图每帧安全地送入 GPU 供波浪变形使用
    if(m_CurrentLayout == VK_IMAGE_LAYOUT_UNDEFINED){
        // 首次上传：UNDEFINED → TRANSFER_DST_OPTIMAL
        // 图像刚创建好，内容是未定义的，直接从 UNDEFINED 切换到传输目标布局即可
        // VkCommandBuffer commandBuffer,
        // VkImageLayout oldLayout,
        // VkImageLayout newLayout,
        // VkPipelineStageFlags srcStage,
        // VkAccessFlags srcAccess,
        // VkPipelineStageFlags dstStage,
        // VkAccessFlags dstAccess
        RecordTransition(
            commandBuffer,
            VK_IMAGE_LAYOUT_UNDEFINED,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
            0,
            VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_ACCESS_TRANSFER_WRITE_BIT
        );
    }
    else{
        // 后续上传：SHADER_READ_ONLY_OPTIMAL → TRANSFER_DST_OPTIMAL
        // 必须先等所有正在进行的着色器读取完成，再切换到传输目标布局
        // VkCommandBuffer commandBuffer,
        // VkImageLayout oldLayout,
        // VkImageLayout newLayout,
        // VkPipelineStageFlags srcStage,
        // VkAccessFlags srcAccess,
        // VkPipelineStageFlags dstStage,
        // VkAccessFlags dstAccess
        RecordTransition(
            commandBuffer,
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            VK_PIPELINE_STAGE_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
            VK_ACCESS_SHADER_READ_BIT,
            VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_ACCESS_TRANSFER_WRITE_BIT
        );
    }

    // 拷贝数据
    VkBufferImageCopy region{};
    region.bufferOffset = 0;
    region.bufferRowLength = 0;
    region.bufferImageHeight = 0;
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.mipLevel = 0;
    region.imageSubresource.baseArrayLayer = 0;
    region.imageSubresource.layerCount = 1;
    region.imageOffset = {0, 0, 0};
    region.imageExtent = {m_Width, m_Height, 1};

    // 将 staging buffer 中的像素数据（例如你每帧计算的位移图）拷贝到 m_Image
    vkCmdCopyBufferToImage(
        commandBuffer,
        stagingBuffer,
        m_Image,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        1,
        &region
    );

    // 将布局切换为 SHADER_READ_ONLY_OPTIMAL，
    // 并插入管线屏障：等待所有传输写入彻底完成，再允许顶点/片段着色器读取
    RecordTransition(
        commandBuffer,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_ACCESS_TRANSFER_WRITE_BIT,
        VK_PIPELINE_STAGE_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
        VK_ACCESS_SHADER_READ_BIT
    );
}

// 用于更新描述符集时绑定图像资源的数据结构，专门把 VkImageView 和 VkSampler 组合到一起，
// 并指定图像布局，告诉着色器“该 binding 用哪张图、怎么采样、处于什么布局”
VkDescriptorImageInfo DynamicImage2D::GetDescriptorInfo(VkSampler sampler) const
{
    VkDescriptorImageInfo info{};
    info.sampler = sampler;
    info.imageView = m_ImageView;
    info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL; // 描述符更新时图像必须处于的布局，这里是着色器读取布局
    return info;
}



}