#include "scene/water/render/StaticDataTexture2D.h"

#include "vulkan/Buffer.h"

#include <stdexcept>

namespace water
{
StaticDataTexture2D::StaticDataTexture2D(
    VkPhysicalDevice physicalDevice,
    VkDevice device,
    vkp::CommandPool& commandPool,
    VkQueue queue,
    uint32_t width,
    uint32_t height,
    VkFormat format,
    const void* data,
    VkDeviceSize dataSize
)
    : m_PhysicalDevice(physicalDevice),
      m_Device(device),
      m_Width(width),
      m_Height(height),
      m_Format(format)
{
    VkDeviceSize expectedSize =
        static_cast<VkDeviceSize>(m_Width) *
        static_cast<VkDeviceSize>(m_Height) *
        GetBytesPerTexel();

    if(dataSize != expectedSize){
        throw std::runtime_error("StaticDataTexture2D data size mismatch");
    }

    CreateImage();
    CreateImageView();
    UploadData(commandPool, queue, data, dataSize);
}

StaticDataTexture2D::~StaticDataTexture2D()
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

VkDescriptorImageInfo StaticDataTexture2D::GetDescriptorInfo(VkSampler sampler) const
{
    VkDescriptorImageInfo info{};
    info.sampler = sampler;
    info.imageView = m_ImageView;
    info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    return info;
}

VkFormat StaticDataTexture2D::GetFormat() const
{
    return m_Format;
}

VkDeviceSize StaticDataTexture2D::GetBytesPerTexel() const
{
    if(m_Format == VK_FORMAT_R16G16B16A16_SFLOAT){
        return 8;
    }

    if(m_Format == VK_FORMAT_R32G32B32A32_SFLOAT){
        return 16;
    }

    throw std::runtime_error("Unsupported StaticDataTexture2D format");
}

uint32_t StaticDataTexture2D::FindMemoryType(
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

    throw std::runtime_error("StaticDataTexture2D failed to find memory type");
}

void StaticDataTexture2D::CreateImage()
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
    imageInfo.usage =
        VK_IMAGE_USAGE_TRANSFER_DST_BIT |
        VK_IMAGE_USAGE_SAMPLED_BIT;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    if(vkCreateImage(m_Device, &imageInfo, nullptr, &m_Image) != VK_SUCCESS){
        throw std::runtime_error("Failed to create StaticDataTexture2D image");
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
        throw std::runtime_error("Failed to allocate StaticDataTexture2D memory");
    }

    vkBindImageMemory(m_Device, m_Image, m_Memory, 0);
}

void StaticDataTexture2D::CreateImageView()
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
        throw std::runtime_error("Failed to create StaticDataTexture2D image view");
    }
}

void StaticDataTexture2D::RecordTransition(
    VkCommandBuffer commandBuffer,
    VkImageLayout oldLayout,
    VkImageLayout newLayout,
    VkPipelineStageFlags srcStage,
    VkAccessFlags srcAccess,
    VkPipelineStageFlags dstStage,
    VkAccessFlags dstAccess
)
{
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
}

void StaticDataTexture2D::UploadData(
    vkp::CommandPool& commandPool,
    VkQueue queue,
    const void* data,
    VkDeviceSize dataSize
)
{
    vkp::Buffer stagingBuffer(
        m_PhysicalDevice,
        m_Device,
        dataSize,
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
            VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
    );

    stagingBuffer.Map();
    stagingBuffer.CopyToMapped(data, dataSize);
    stagingBuffer.Unmap();

    VkCommandBuffer commandBuffer =
        commandPool.BeginOneTimeCommands(m_Device);

    RecordTransition(
        commandBuffer,
        VK_IMAGE_LAYOUT_UNDEFINED,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
        0,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_ACCESS_TRANSFER_WRITE_BIT
    );

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

    vkCmdCopyBufferToImage(
        commandBuffer,
        stagingBuffer,
        m_Image,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        1,
        &region
    );

    RecordTransition(
        commandBuffer,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_ACCESS_TRANSFER_WRITE_BIT,
        VK_PIPELINE_STAGE_VERTEX_SHADER_BIT |
            VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
        VK_ACCESS_SHADER_READ_BIT
    );

    commandPool.EndOneTimeCommands(
        m_Device,
        queue,
        commandBuffer
    );
}
}