#include "vulkan/Sampler.h"

#include <stdexcept>

namespace vkp
{
Sampler::Sampler(VkDevice device)
    : m_Device(device)
{
    CreateSampler();
}

Sampler::~Sampler()
{
    if(m_Sampler != VK_NULL_HANDLE){
        vkDestroySampler(m_Device, m_Sampler, nullptr);
    }
}

Sampler::operator VkSampler() const
{
    return m_Sampler;
}

VkSampler Sampler::GetHandle() const
{
    return m_Sampler;
}

void Sample::CreateSampler(){ // 创建采样器
    VkSamplerCreateInfo samplerInfo{}; // 采样器创建信息
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO; // 结构类型

    samplerInfo.magFilter = VK_FILTER_LINEAR; // 放大过滤器, 用于放大图像时的过滤方式, 线性插值
    samplerInfo.minFilter = VK_FILTER_LINEAR; // 缩小过滤器, 用于缩小图像时的过滤方式, 线性插值

    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT; // U 方向的采样器地址模式, 重复 
    // 例如 (1.2, 0.5) 等效于 (0.2, 0.5)。常用在砖墙等平铺纹理
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT; // V 方向的采样器地址模式, 重复
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT; // W 方向的采样器地址模式, 重复

    samplerInfo.anisotropyEnable = VK_FALSE; // 是否启用各向异性过滤 减少倾斜视角下的纹理模糊
    samplerInfo.maxAnisotropy = 1.0f; // 各向异性过滤的最大程度

    samplerInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK; // 寻址模式为 CLAMP_TO_BORDER 时，超出范围的颜色为不透明黑色
    samplerInfo.unnormalizedCoordinates = VK_FALSE; // 使用规范化的纹理坐标（0~1）。如果为 TRUE，则直接用像素坐标

    samplerInfo.compareEnable = VK_FALSE; // 不使用深度比较模式（用于阴影贴图）
    samplerInfo.compareOp = VK_COMPARE_OP_ALWAYS; // 比较操作

    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR; // 多级渐远纹理过滤模式 线性混合两层 mipmap
    samplerInfo.mipLodBias = 0.0f; // 多级渐远纹理偏移
    samplerInfo.minLod = 0.0f; // 多级渐远纹理最小级别
    samplerInfo.maxLod = 0.0f; // 多级渐远纹理最大级别

    if (vkCreateSampler(m_Device, &samplerInfo, nullptr, &m_Sampler) != VK_SUCCESS) { // 创建采样器
        throw std::runtime_error("Failed to create texture sampler!");
    }
}


}