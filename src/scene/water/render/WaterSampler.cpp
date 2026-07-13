#include "scene/water/render/WaterSampler.h"

#include <stdexcept>

namespace water
{
WaterSampler::WaterSampler(VkDevice device, VkFilter filter)
    : m_Device(device)
{
    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = filter;
    samplerInfo.minFilter = filter;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;

    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;

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