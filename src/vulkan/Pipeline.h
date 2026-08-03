#pragma once

#include <vulkan/vulkan.h>

#include <string>
#include <vector>

namespace vkp
{
struct PipelineConfig
{
    std::vector<VkVertexInputBindingDescription> bindingDescriptions; // 顶点输入绑定描述
    std::vector<VkVertexInputAttributeDescription> attributeDescriptions; // 顶点输入属性描述
    std::vector<VkDescriptorSetLayout> descriptorSetLayouts;  // 描述符集布局
    std::vector<VkPushConstantRange> pushConstantRanges;      // 推送常量范围 避免创建每 Tile UBO

    bool depthTestEnable = false;                             // 深度测试是否启用
    bool depthWriteEnable = false;                            // 深度写入是否启用
    VkCompareOp depthCompareOp = VK_COMPARE_OP_LESS;          // 深度比较操作，表示新深度小于旧深度时通过

    VkPolygonMode polygonMode = VK_POLYGON_MODE_FILL;         // 多边形模式，填充
    VkCullModeFlags cullMode = VK_CULL_MODE_BACK_BIT;         // 剔除模式， GPU 自动剔除背面三角形
    VkFrontFace frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;  // 正面朝向，定义了哪个方向是“正面”
    bool blendEnable = false;                                 // 是否开启 alpha 混合（半透明水体用）
};
class Pipeline
{
public:
    Pipeline(
        VkDevice device,
        VkRenderPass renderPass,
        const std::string& vertShaderPath,
        const std::string& fragShaderPath,
        const PipelineConfig& config
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
        const std::string& fragShaderPath,
        const PipelineConfig& config
    ); // 创建图形管线

private:
    VkDevice m_Device = VK_NULL_HANDLE;                       // 逻辑设备句柄
    VkPipelineLayout m_PipelineLayout = VK_NULL_HANDLE;       // 管线布局，用于描述管线的输入和输出
    VkPipeline m_Pipeline = VK_NULL_HANDLE;                   // 图形管线，用于描述图形渲染过程
};
}