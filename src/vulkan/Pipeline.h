#pragma once

#include <vulkan/vulkan.h>

#include <string>

namespace vkp
{
class Pipeline
{
public:
    Pipeline(
        VkDevice device,
        VkRenderPass renderPass,
        const std::string& vertShaderPath,
        const std::string& fragShaderPath
    );
    ~Pipeline();

    Pipeline(const Pipeline&) = delete;
    Pipeline& operator=(const Pipeline&) = delete;

    operator VkPipeline() const;
    VkPipeline GetHandle() const;
    VkPipelineLayout GetLayout() const;

private:
    void createGraphicsPipeline(
        VkRenderPass renderPass,
        const std::string& vertShaderPath,
        const std::string& fragShaderPath
    ); // 创建图形管线

private:
    VkDevice m_Device = VK_NULL_HANDLE;                       // 逻辑设备句柄
    VkPipelineLayout m_PipelineLayout = VK_NULL_HANDLE;       // 管线布局，用于描述管线的输入和输出
    VkPipeline m_Pipeline = VK_NULL_HANDLE;                   // 图形管线，用于描述图形渲染过程
};
}