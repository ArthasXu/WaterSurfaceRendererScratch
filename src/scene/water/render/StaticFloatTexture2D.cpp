#include "scene/water/render/StaticFloatTexture2D.h"

#include <stdexcept>

// 封装了一张一次性上传、只读采样的静态浮点纹理，用于存储预计算的 LUT 数据（如 Front LUT、Wave Profile 等），
// 并通过 Vulkan 管线屏障自动完成布局转换，最终以 SHADER_READ_ONLY_OPTIMAL 状态供着色器采样
namespace water
{
// 校验像素数据大小是否匹配，然后依次调用 CreateImage()、CreateImageView() 和 UploadPixels()，
// 完成图像创建、视图创建和数据上传的全流程。
// 构造完成后，纹理已处于 SHADER_READ_ONLY_OPTIMAL 布局，可直接用于着色器采样
StaticFloatTexture2D::StaticFloatTexture2D(
    VkPhysicalDevice physicalDevice,
    VkDevice device,
    vkp::CommandPool& commandPool,
    VkQueue queue,
    uint32_t width,
    uint32_t height,
    const std::vector<glm::vec4>& pixels
)
    : m_PhysicalDevice(physicalDevice),
      m_Device(device),
      m_Width(width),
      m_Height(height)
{
    if(pixels.size() != static_cast<size_t>(width) * static_cast<size_t>(height)){
        throw std::runtime_error("StaticFloatTexture2D pixel count mismatch");
    }

    CreateImage();
    CreateImageView();
    UploadPixels(commandPool, queue, pixels);
}

// 按逆序销毁图像视图、图像和设备内存，确保 Vulkan 资源被正确释放
StaticFloatTexture2D::~StaticFloatTexture2D()
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

VkImage StaticFloatTexture2D::GetImage() const
{
    return m_Image;
}

VkImageView StaticFloatTexture2D::GetImageView() const
{
    return m_ImageView;
}

// 构建并返回 VkDescriptorImageInfo，封装了采样器、图像视图和当前布局（SHADER_READ_ONLY_OPTIMAL）。
// 这个结构体可直接用于 DescriptorWriter 写入描述符集，让着色器能采样这张纹理
VkDescriptorImageInfo StaticFloatTexture2D::GetDescriptorInfo(VkSampler sampler) const
{
    VkDescriptorImageInfo info{};
    info.sampler = sampler;
    info.imageView = m_ImageView;
    info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    return info;
}

// 遍历物理设备的内存类型，找到同时满足 typeFilter 位掩码
// 和所需 properties（如 DEVICE_LOCAL）的内存类型索引。用于为图像分配合适的显存
uint32_t StaticFloatTexture2D::FindMemoryType(
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

    throw std::runtime_error("StaticFloatTexture2D failed to find memory type");
}

// 创建 VkImage 对象，指定为 2D 纹理，格式为 VK_FORMAT_R32G32B32A32_SFLOAT，
// 用途为 TRANSFER_DST | SAMPLED（即接受数据传输 + 可被着色器采样）。
// 然后查询内存需求，分配 DEVICE_LOCAL 显存，并将内存绑定到图像
void StaticFloatTexture2D::CreateImage()
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
        throw std::runtime_error("Failed to create StaticFloatTexture2D image");
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
        throw std::runtime_error("Failed to allocate StaticFloatTexture2D memory");
    }

    vkBindImageMemory(m_Device, m_Image, m_Memory, 0);
}

// 为已创建的 VkImage 创建一个 2D 图像视图，指定格式和颜色分量，使图像能够被 Vulkan 管线作为纹理采样访问
void StaticFloatTexture2D::CreateImageView()
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
        throw std::runtime_error("Failed to create StaticFloatTexture2D image view");
    }
}

// 录制一条图像管线屏障命令，将图像从旧布局转换为新布局，并设置相应的源/目标管线阶段和访问掩码。
// 这是 Vulkan 中保证图像数据在管线阶段之间正确可见和布局一致的核心同步操作
void StaticFloatTexture2D::RecordTransition(
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

// 将 CPU 端的 pixels 数据上传到设备本地图像。具体步骤：
// 1. 创建 staging buffer（HOST_VISIBLE | HOST_COHERENT），映射并拷贝像素数据。
// 2. 分配一次性命令缓冲，录制以下命令：
    // 布局转换：UNDEFINED → TRANSFER_DST_OPTIMAL（准备接收数据）
    // vkCmdCopyBufferToImage：从 staging buffer 拷贝到图像
    // 布局转换：TRANSFER_DST_OPTIMAL → SHADER_READ_ONLY_OPTIMAL（变为采样可读）
// 3. 提交命令并等待队列执行完成，staging buffer 自动销毁。
void StaticFloatTexture2D::UploadPixels(
    vkp::CommandPool& commandPool,
    VkQueue queue,
    const std::vector<glm::vec4>& pixels
)
{
    VkDeviceSize uploadSize =
        static_cast<VkDeviceSize>(pixels.size()) *
        sizeof(glm::vec4);

    vkp::Buffer stagingBuffer(
        m_PhysicalDevice,
        m_Device,
        uploadSize,
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
            VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
    );

    stagingBuffer.Map();
    stagingBuffer.CopyToMapped(pixels.data(), uploadSize);
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



