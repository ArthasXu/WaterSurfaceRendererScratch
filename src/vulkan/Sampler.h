#pragma once

#include <vulkan/vulkan.h>

namespace vkp
{
class Sampler
{
public:
    explicit Sampler(VkDevice device);
    ~Sampler();

    Sampler(const Sampler&) = delete;
    Sampler& operator=(const Sampler&) = delete;

    operator VkSampler() const;
    VkSampler GetHandle() const;

private:
    void CreateSampler(); // 创建采样器

private:
    VkDevice m_Device = VK_NULL_HANDLE; // 逻辑设备句柄
    VkSampler m_Sampler = VK_NULL_HANDLE; // 采样器句柄
};
}