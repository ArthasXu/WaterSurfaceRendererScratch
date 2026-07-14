#pragma once

#include <vulkan/vulkan.h>

#include <cstdint>

namespace water
{
class DynamicImage2D
{
public:
    // 服务于 Stage 6 的“静态网格 + 采样位移”方案
    DynamicImage2D(
        VkPhysicalDevice physicalDevice,
        VkDevice device,
        uint32_t width,
        uint32_t height,
        VkFormat format,
        VkImageUsageFlags usage
    );

    ~DynamicImage2D();

    DynamicImage2D(const DynamicImage2D&) = delete;
    DynamicImage2D& operator=(const DynamicImage2D&) = delete;

    VkImage GetImage() const;
    VkImageView GetImageView() const;
    VkFormat GetFormat() const;
    uint32_t GetWidth() const;
    uint32_t GetHeight() const;

    void RecordUpload(
        VkCommandBuffer commandBuffer,
        VkBuffer stagingBuffer
    );

    // 将图像布局转换为 VK_IMAGE_LAYOUT_GENERAL，允许计算着色器以通用方式读写图像
    // （通常用于 compute shader 直接通过 imageStore 写入）。这是 compute 写入前的必要准备。
    void RecordTransitionToGeneral(VkCommandBuffer commandBuffer);

    // 在 compute shader 写入图像数据后调用。它将图像布局从 GENERAL（或适合 compute 写入的布局）
    // 转换为 SHADER_READ_ONLY_OPTIMAL，并插入管线屏障：等待所有 compute 写入完成，再允许顶点/片段着色器读取。
    // 这是 GPU FFT 结果进入图形管线的关键同步点。
    void RecordComputeWriteToGraphicsReadBarrier(VkCommandBuffer commandBuffer);

    // 反向操作：当下一帧需要重新用 compute shader 更新同一张图像时，
    // 必须先将布局从 SHADER_READ_ONLY_OPTIMAL 转回适合 compute 写入的布局，并等待图形着色器读取完成。
    // 这防止了 compute 写入与正在进行的图形读取冲突
    void RecordGraphicsReadToComputeWriteBarrier(VkCommandBuffer commandBuffer);

    // 用于从 compute 写入的图像中拷贝数据（例如回读到 CPU 或 staging buffer）。
    // 它将布局转换为 TRANSFER_SRC_OPTIMAL，确保 compute 写入已完全对传输操作可见。
    void RecordComputeWriteToTransferReadBarrier(VkCommandBuffer commandBuffer);

    // 当图像处于 SHADER_READ_ONLY_OPTIMAL 布局时，作为采样纹理绑定到图形或计算管线
    // 对应场景：顶点着色器采样位移图来变形水面网格。
    VkDescriptorImageInfo GetDescriptorInfo(VkSampler sampler) const;
    // 当图像处于 VK_IMAGE_LAYOUT_GENERAL 布局时，作为采样纹理绑定
    // 对应场景：Compute Shader 在写入后、但布局尚未转为只读时，可能需要读取自己刚写入的数据（例如某种原地计算），或者在通用布局下同时读写
    VkDescriptorImageInfo GetGeneralSampledDescriptorInfo(VkSampler sampler) const;
    // 当图像处于 VK_IMAGE_LAYOUT_GENERAL 布局时，作为存储图像绑定
    // 对应场景：Compute Shader 通过 imageStore 直接将计算结果（如位移、法线辅助数据）写入纹理，完全绕过传统的渲染通道和图形管线
    VkDescriptorImageInfo GetStorageDescriptorInfo() const;

private:
    // 设备本地内存 VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT GPU 专用显存，CPU 不能直接映射读写，带宽最高 用于静态网格、纹理等渲染资源
    // 主机可见内存 VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT CPU 可以通过映射（vkMapMemory）直接读写。通常与以下两个属性组合
    // 主机一致内存	HOST_VISIBLE | HOST_COHERENT CPU 写入后自动对 GPU 可见，无需手动 flush。常用于 uniform 缓冲、动态顶点缓冲
    // 主机缓存内存	HOST_VISIBLE | HOST_CACHED CPU 写入后需要手动 flush，才能对 GPU 可见。常用于 staging buffer
    // 懒分配内存	LAZILY_ALLOCATED	仅在部分 GPU（如 NVIDIA）上支持，用于延迟分配大块内存（如 MSAA 中间缓冲），驱动按需分配。
    uint32_t FindMemoryType(
        uint32_t typeFilter,
        VkMemoryPropertyFlags properties
    ) const;

    void CreateImage(VkImageUsageFlags usage);
    void CreateImageView();

    void RecordTransition(
        VkCommandBuffer commandBuffer,
        VkImageLayout oldLayout,
        VkImageLayout newLayout,
        VkPipelineStageFlags srcStage,
        VkAccessFlags srcAccess,
        VkPipelineStageFlags dstStage,
        VkAccessFlags dstAccess
    );

private:
    VkPhysicalDevice m_PhysicalDevice = VK_NULL_HANDLE;
    VkDevice m_Device = VK_NULL_HANDLE;

    uint32_t m_Width = 0;
    uint32_t m_Height = 0;
    VkFormat m_Format = VK_FORMAT_UNDEFINED;

    VkImage m_Image = VK_NULL_HANDLE;
    VkDeviceMemory m_Memory = VK_NULL_HANDLE;
    VkImageView m_ImageView = VK_NULL_HANDLE;

    VkImageLayout m_CurrentLayout = VK_IMAGE_LAYOUT_UNDEFINED;
};
}