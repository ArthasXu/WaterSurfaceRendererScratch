#pragma once

#include <vulkan/vulkan.h>

#include <cstdint>
#include <string>
#include <vector>

namespace water
{
struct ComputePipelineConfig
{
    std::vector<VkDescriptorSetLayout> descriptorSetLayouts;

    bool enablePushConstants = false;
    VkShaderStageFlags pushConstantStageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    uint32_t pushConstantSize = 0;
    uint32_t pushConstantOffset = 0;
};

// Vulkan 计算管线（Compute Pipeline）的 RAII 封装类
class ComputePipeline
{
public:
    ComputePipeline(
        VkDevice device,
        const std::string& computeShaderPath,
        const ComputePipelineConfig& config
    );

    ~ComputePipeline();

    ComputePipeline(const ComputePipeline&) = delete;
    ComputePipeline& operator=(const ComputePipeline&) = delete;

    operator VkPipeline() const;
    VkPipeline GetHandle() const;
    VkPipelineLayout GetLayout() const;

private:
    void CreatePipeline(
        const std::string& computeShaderPath,
        const ComputePipelineConfig& config
    );

private:
    VkDevice m_Device = VK_NULL_HANDLE;
    VkPipelineLayout m_PipelineLayout = VK_NULL_HANDLE;
    VkPipeline m_Pipeline = VK_NULL_HANDLE;
};
}