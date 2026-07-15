#include "scene/water/render/WaterSampler.h"

#include <stdexcept>

namespace water
{
WaterSampler::WaterSampler(
    VkDevice device,
    VkFilter filter,
    VkSamplerAddressMode addressMode
)
    : m_Device(device)
{
    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = filter;
    samplerInfo.minFilter = filter;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;

    // 控制的是当纹理坐标（UV）超出 0 到 1 范围时，GPU 如何读取纹理
    // VK_SAMPLER_ADDRESS_MODE_REPEAT：纹理坐标超出范围时，会重复使用纹理的边缘像素。
    // CLAMP_TO_EDGE：纹理坐标超出范围时，会使用边缘像素进行填充。坐标小于 0 就取 0 处的颜色，大于 1 就取 1 处的颜色。它相当于把纹理边缘的颜色向外无限延伸
        // Front LUT 是一张一维的查找表，描述的是波前在线性空间上的属性分布。
        // 波前线的长度是 1000 米，当顶点投影到波前线的两端之外时（比如横向距离超过了 ±500 米），就不应再出现新的波形变化了
    samplerInfo.addressModeU = addressMode;
    samplerInfo.addressModeV = addressMode;
    samplerInfo.addressModeW = addressMode;

    samplerInfo.mipLodBias = 0.0f;
    samplerInfo.minLod = 0.0f;
    samplerInfo.maxLod = 0.0f;

    samplerInfo.anisotropyEnable = VK_FALSE;
    samplerInfo.maxAnisotropy = 1.0f;

    samplerInfo.compareEnable = VK_FALSE;
    samplerInfo.compareOp = VK_COMPARE_OP_ALWAYS;

    samplerInfo.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK;
    samplerInfo.unnormalizedCoordinates = VK_FALSE;

    if(vkCreateSampler(m_Device, &samplerInfo, nullptr, &m_Sampler) != VK_SUCCESS){
        throw std::runtime_error("Failed to create WaterSampler");
    }
}

WaterSampler::~WaterSampler()
{
    if(m_Sampler != VK_NULL_HANDLE){
        vkDestroySampler(m_Device, m_Sampler, nullptr);
    }
}

WaterSampler::operator VkSampler() const
{
    return m_Sampler;
}

VkSampler WaterSampler::GetHandle() const
{
    return m_Sampler;
}
}