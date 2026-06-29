#include "vulkan/Image.h"

#include <stdexcept>

namespace vkp
{
Image::Image(
    VkPhysicalDevice physicalDevice,
    VkDevice device,
    uint32_t width,
    uint32_t height,
    VkFormat format,
    VkImageTiling tiling,
    VkImageUsageFlags usage,
    VkMemoryPropertyFlags properties
)
    : m_Device(device),
      m_PhysicalDevice(physicalDevice),
      m_Format(format),
      m_Width(width),
      m_Height(height)
{
    CreateImage(width, height, format, tiling, usage, properties);
}

Image::~Image()
{
    if(m_Image != VK_NULL_HANDLE){
        vkDestroyImage(m_Device, m_Image, nullptr);
    }

    if(m_Memory != VK_NULL_HANDLE){
        vkFreeMemory(m_Device, m_Memory, nullptr);
    }
}

Image::operator VkImage() const
{
    return m_Image;
}

VkImage Image::GetHandle() const
{
    return m_Image;
}

VkFormat Image::GetFormat() const
{
    return m_Format;
}

uint32_t Image::GetWidth() const
{
    return m_Width;
}

uint32_t Image::GetHeight() const
{
    return m_Height;
}

void Image::CreateImage(
    uint32_t width,
    uint32_t height,
    VkFormat format,
    VkImageTiling tiling,
    VkImageUsageFlags usage,
    VkMemoryPropertyFlags properties
){
    VkImageCreateInfo imageInfo{}; // 图像创建信息
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO; // 结构体类型
    imageInfo.imageType = VK_IMAGE_TYPE_2D; // 图像类型
    imageInfo.extent.width = width; // 图像宽度
    imageInfo.extent.height = height; // 图像高度
    imageInfo.extent.depth = 1; // 图像深度
    imageInfo.mipLevels = 1; // 图像层级
    imageInfo.arrayLayers = 1; // 图像数组层数
    imageInfo.format = format; // 图像格式
    imageInfo.tiling = tiling; // 图像平铺方式
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED; // 图像初始布局
    imageInfo.usage = usage; // 图像用途
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT; // 图像采样数
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE; // 图像共享模式

    if(vkCreateImage(m_Device, &imageInfo, nullptr, &m_Image) != VK_SUCCESS){
        throw std::runtime_error("failed to create image!");
    }

    VkMemoryRequirements memRequirements; // 内存需求
    vkGetImageMemoryRequirements(m_Device, m_Image, &memRequirements); // 获取图像内存需求

    VkMemoryAllocateInfo allocInfo{}; // 内存分配信息
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO; // 结构体类型
    allocInfo.allocationSize = memRequirements.size; // 内存分配大小
    allocInfo.memoryTypeIndex = FindMemoryType(memRequirements.memoryTypeBits, properties); // 内存类型索引

    if(vkAllocateMemory(m_Device, &allocInfo, nullptr, &m_Memory) != VK_SUCCESS){
        throw std::runtime_error("failed to allocate image memory!");
    }

    vkBindImageMemory(m_Device, m_Image, m_Memory, 0); // 绑定图像内存
}

uint32_t Image::FindMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties) const {
    VkPhysicalDeviceMemoryProperties memProperties{}; // 物理设备内存属性
    vkGetPhysicalDeviceMemoryProperties(m_PhysicalDevice, &memProperties); // 获取物理设备内存属性

    for(uint32_t i = 0; i < memProperties.memoryTypeCount; i++){
        if((typeFilter & (1 << i)) &&
           (memProperties.memoryTypes[i].propertyFlags & properties) == properties){
            return i;
        }
    }

    throw std::runtime_error("failed to find suitable memory type!");
}

// 调用 vkCmdPipelineBarrier（通过一次性命令缓冲）
// 将图像从一种布局（如 UNDEFINED、TRANSFER_DST_OPTIMAL）
// 转换到另一种（如 SHADER_READ_ONLY_OPTIMAL）
void Image::TransitionLayout(
    VkCommandPool commandPool,
    VkQueue queue,
    VkImageLayout oldLayout,
    VkImageLayout newLayout
){
    VkCommandBufferAllocateInfo allocInfo{}; // 一次性命令缓冲分配信息
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO; // 结构体类型
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY; // 命令缓冲级别
    allocInfo.commandPool = commandPool; // 命令池
    allocInfo.commandBufferCount = 1; // 命令缓冲数量

    VkCommandBuffer commandBuffer = VK_NULL_HANDLE; // 一次性命令缓冲
    vkAllocateCommandBuffers(m_Device, &allocInfo, &commandBuffer); // 分配一次性命令缓冲

    VkCommandBufferBeginInfo beginInfo{}; // 一次性命令缓冲开始信息
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO; // 结构体类型
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT; // 命令缓冲使用标志

    vkBeginCommandBuffer(commandBuffer, &beginInfo); // 开始一次性命令缓冲

    VkImageMemoryBarrier barrier{}; // 图像内存屏障
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER; // 结构体类型
    barrier.oldLayout = oldLayout; // 旧布局
    barrier.newLayout = newLayout; // 新布局
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED; // 源队列族索引
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED; // 目标队列族索引
    barrier.image = m_Image; // 图像
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT; // 子资源范围
    barrier.subresourceRange.baseMipLevel = 0; // 基础层级
    barrier.subresourceRange.levelCount = 1; // 层级数量
    barrier.subresourceRange.baseArrayLayer = 0; // 基础数组层
    barrier.subresourceRange.layerCount = 1; // 数组层数量

    VkPipelineStageFlags sourceStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT; // 源阶段, 表示命令缓冲的开始
    VkPipelineStageFlags destinationStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT; // 目标阶段, 表示命令缓冲的结束

    if(oldLayout == VK_IMAGE_LAYOUT_UNDEFINED && 
        newLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL){ 
        // 如果旧布局是 UNDEFINED，新布局是 TRANSFER_DST_OPTIMAL, 表示图像将被用作传输目标
        barrier.srcAccessMask = 0; // 源访问掩码
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT; // 目标访问掩码, 表示传输写入

        sourceStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT; // 源阶段, 表示命令缓冲的开始
        destinationStage = VK_PIPELINE_STAGE_TRANSFER_BIT; // 目标阶段, 表示传输阶段
    }
    else if(oldLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL &&
            newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL){
        // 如果旧布局是 TRANSFER_DST_OPTIMAL，新布局是 SHADER_READ_ONLY_OPTIMAL
        // 表示图像将被用作着色器只读
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT; // 源访问掩码, 表示传输写入
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT; // 目标访问掩码, 表示着色器读取

        sourceStage = VK_PIPELINE_STAGE_TRANSFER_BIT; // 源阶段, 表示传输阶段
        destinationStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT; // 目标阶段, 表示片段着色器阶段
    }
    else{
        throw std::runtime_error("Unsupported image layout transition!"); // 不支持的布局转换
    }

    // void vkCmdPipelineBarrier(
    //     VkCommandBuffer                             commandBuffer, // 命令缓冲
    //     VkPipelineStageFlags                        srcStageMask, // 源阶段掩码
    //     VkPipelineStageFlags                        dstStageMask, // 目标阶段掩码
    //     VkDependencyFlags                           dependencyFlags, // 依赖标志
    //     uint32_t                                    memoryBarrierCount, // 内存屏障数量
    //     const VkMemoryBarrier*                      pMemoryBarriers, // 内存屏障
    //     uint32_t                                    bufferMemoryBarrierCount, // 缓冲区内存屏障数量
    //     const VkBufferMemoryBarrier*                pBufferMemoryBarriers, // 缓冲区内存屏障
    //     uint32_t                                    imageMemoryBarrierCount, // 图像内存屏障数量
    //     const VkImageMemoryBarrier*                 pImageMemoryBarriers // 图像内存屏障
    // );
    vkCmdPipelineBarrier(
        commandBuffer,
        sourceStage,
        destinationStage,
        0,
        0,
        nullptr,
        0,
        nullptr,
        1,
        &barrier
    ); // 管道屏障

    vkEndCommandBuffer(commandBuffer); // 结束一次性命令缓冲

    VkSubmitInfo submitInfo{}; // 提交信息
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO; // 结构体类型
    submitInfo.commandBufferCount = 1; // 命令缓冲数量
    submitInfo.pCommandBuffers = &commandBuffer; // 命令缓冲数组

    vkQueueSubmit(queue, 1, &submitInfo, VK_NULL_HANDLE); // 提交命令缓冲
    vkQueueWaitIdle(queue); // 等待队列空闲

    vkFreeCommandBuffers(m_Device, commandPool, 1, &commandBuffer); // 释放命令缓冲
}

void Image::CopyFromBuffer(
    VkCommandPool commandPool,
    VkQueue queue,
    VkBuffer buffer
){ // 从缓冲区拷贝数据到图像
    VkCommandBufferAllocateInfo allocInfo{}; // 一次性命令缓冲分配信息
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO; // 结构体类型
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY; // 命令缓冲级别
    allocInfo.commandPool = commandPool; // 命令池
    allocInfo.commandBufferCount = 1; // 命令缓冲数量

    VkCommandBuffer commandBuffer = VK_NULL_HANDLE; // 一次性命令缓冲
    vkAllocateCommandBuffers(m_Device, &allocInfo, &commandBuffer); // 分配一次性命令缓冲

    VkCommandBufferBeginInfo beginInfo{}; // 一次性命令缓冲开始信息
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO; // 结构体类型
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT; // 命令缓冲使用标志

    vkBeginCommandBuffer(commandBuffer, &beginInfo); // 开始一次性命令缓冲

    // typedef struct VkBufferImageCopy {
    //     VkDeviceSize                bufferOffset;        // 缓冲区偏移量
    //     uint32_t                    bufferRowLength;     // 缓冲区行长度
    //     uint32_t                    bufferImageHeight;   // 缓冲区图像高度
    //     VkImageSubresourceLayers    imageSubresource;    // 图像子资源
    //     VkOffset3D                  imageOffset;         // 图像偏移量
    //     VkExtent3D                  imageExtent;         // 图像扩展
    // } VkBufferImageCopy; // 缓冲区图像拷贝
    VkBufferImageCopy region{}; // 缓冲区图像拷贝区域
    region.bufferOffset = 0; // 缓冲区偏移量
    region.bufferRowLength = 0; // 缓冲区行长度
    region.bufferImageHeight = 0; // 缓冲区图像高度

    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT; // 图像子资源
    region.imageSubresource.mipLevel = 0; // 层级
    region.imageSubresource.baseArrayLayer = 0; // 基础数组层
    region.imageSubresource.layerCount = 1; // 数组层数量

    region.imageOffset = { 0, 0, 0 }; // 图像偏移量
    region.imageExtent = { m_Width, m_Height, 1 }; // 图像扩展

    vkCmdCopyBufferToImage(
        commandBuffer,
        buffer,
        m_Image,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        1,
        &region
    ); // 从缓冲区拷贝数据到图像

    vkEndCommandBuffer(commandBuffer); // 结束一次性命令缓冲

    VkSubmitInfo submitInfo{}; // 提交信息
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO; // 结构体类型
    submitInfo.commandBufferCount = 1; // 命令缓冲数量
    submitInfo.pCommandBuffers = &commandBuffer; // 命令缓冲数组

    vkQueueSubmit(queue, 1, &submitInfo, VK_NULL_HANDLE); // 提交命令缓冲
    vkQueueWaitIdle(queue); // 等待队列空闲

    vkFreeCommandBuffers(m_Device, commandPool, 1, &commandBuffer); // 释放命令缓冲
}

}