#pragma once

#include <vulkan/vulkan.h>

namespace water
{
class WaterSampler
{
public:
    WaterSampler(
        VkDevice device, 
        VkFilter filter,
        VkSamplerAddressMode addressMode = VK_SAMPLER_ADDRESS_MODE_REPEAT
    );
    ~WaterSampler();

    WaterSampler(const WaterSampler&) = delete;
    WaterSampler& operator=(const WaterSampler&) = delete;

    operator VkSampler() const;
    VkSampler GetHandle() const;

private:
    VkDevice m_Device = VK_NULL_HANDLE;
    VkSampler m_Sampler = VK_NULL_HANDLE;
};
}