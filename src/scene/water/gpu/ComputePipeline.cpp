#include "scene/water/gpu/ComputePipeline.h"

#include "vulkan/ShaderModule.h"

#include <stdexcept>

namespace water
{
ComputePipeline::ComputePipeline(
    VkDevice device,
    const std::string& computeShaderPath,
    const ComputePipelineConfig& config
)
    : m_Device(device)
{
    CreatePipeline(computeShaderPath, config);
}

ComputePipeline::~ComputePipeline()
{
    if(m_Pipeline != VK_NULL_HANDLE){
        vkDestroyPipeline(m_Device, m_Pipeline, nullptr);
    }

    if(m_PipelineLayout != VK_NULL_HANDLE){
        vkDestroyPipelineLayout(m_Device, m_PipelineLayout, nullptr);
    }
}

ComputePipeline::operator VkPipeline() const
{
    return m_Pipeline;
}

VkPipeline ComputePipeline::GetHandle() const
{
    return m_Pipeline;
}

VkPipelineLayout ComputePipeline::GetLayout() const
{
    return m_PipelineLayout;
}

void ComputePipeline::CreatePipeline(
    const std::string& computeShaderPath,
    const ComputePipelineConfig& config
)
{
    vkp::ShaderModule computeShader(m_Device, computeShaderPath);

    // typedef struct VkPushConstantRange {
    //     VkShaderStageFlags    stageFlags; // 这是一个位掩码，用于指定哪些着色器阶段可以访问这个推送常量范围。
    //     uint32_t              offset; // 推送常量数据在着色器中的偏移量。
    //     uint32_t              size; // 推送常量数据的大小（以字节为单位）。
    // } VkPushConstantRange;
    VkPushConstantRange pushConstantRange{};
    pushConstantRange.stageFlags = config.pushConstantStageFlags;
    pushConstantRange.offset = config.pushConstantOffset;
    pushConstantRange.size = config.pushConstantSize;

    VkPipelineLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layoutInfo.setLayoutCount =
        static_cast<uint32_t>(config.descriptorSetLayouts.size());
    layoutInfo.pSetLayouts = config.descriptorSetLayouts.data();

    if(config.enablePushConstants){
        layoutInfo.pushConstantRangeCount = 1;
        layoutInfo.pPushConstantRanges = &pushConstantRange;
    }
    else{
        layoutInfo.pushConstantRangeCount = 0;
        layoutInfo.pPushConstantRanges = nullptr;
    }

    if(vkCreatePipelineLayout(
        m_Device,
        &layoutInfo,
        nullptr,
        &m_PipelineLayout
    ) != VK_SUCCESS){
        throw std::runtime_error("Failed to create compute pipeline layout");
    }

    VkPipelineShaderStageCreateInfo shaderStageInfo{};
    shaderStageInfo.sType =
        VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    shaderStageInfo.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    shaderStageInfo.module = computeShader.GetHandle();
    shaderStageInfo.pName = "main";

    VkComputePipelineCreateInfo pipelineInfo{}; // 计算管线信息
    pipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO; // 结构体类型
    pipelineInfo.stage = shaderStageInfo; // 着色器阶段信息
    pipelineInfo.layout = m_PipelineLayout; // 管线布局
    pipelineInfo.basePipelineHandle = VK_NULL_HANDLE; // 基础管线, 无
    pipelineInfo.basePipelineIndex = -1; // 基础管线索引, -1

    if(vkCreateComputePipelines(
        m_Device,
        VK_NULL_HANDLE,
        1,
        &pipelineInfo,
        nullptr,
        &m_Pipeline
    ) != VK_SUCCESS){
        throw std::runtime_error("Failed to create compute pipeline");
    }
}

}