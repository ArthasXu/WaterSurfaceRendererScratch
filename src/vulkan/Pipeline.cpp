#include "Pipeline.h"
#include "ShaderModule.h"

#include <iostream>
#include <stdexcept>
#include <vector>

namespace vkp
{
Pipeline::Pipeline(
    VkDevice device,
    VkRenderPass renderPass,
    const std::string& vertShaderPath,
    const std::string& fragShaderPath,
    const PipelineConfig& config
)
    : m_Device(device)
{
    createGraphicsPipeline(renderPass, vertShaderPath, fragShaderPath, config);
}

Pipeline::~Pipeline()
{
    if(m_Pipeline != VK_NULL_HANDLE){
        vkDestroyPipeline(m_Device, m_Pipeline, nullptr); // 销毁图形管线
    }

    if(m_PipelineLayout != VK_NULL_HANDLE){
        vkDestroyPipelineLayout(m_Device, m_PipelineLayout, nullptr); // 销毁管线布局
    }
}

Pipeline::operator VkPipeline() const
{
    return m_Pipeline;
}

VkPipeline Pipeline::GetHandle() const
{
    return m_Pipeline;
}

VkPipelineLayout Pipeline::GetLayout() const
{
    return m_PipelineLayout;
}

void Pipeline::createGraphicsPipeline(
    VkRenderPass renderPass,
    const std::string& vertShaderPath,
    const std::string& fragShaderPath,
    const PipelineConfig& config
){
    vkp::ShaderModule vertShaderModule(m_Device, vertShaderPath); // 创建顶点着色器模块
    vkp::ShaderModule fragShaderModule(m_Device, fragShaderPath); // 创建片元着色器模块

    VkPipelineShaderStageCreateInfo vertShaderStageInfo{}; // 顶点着色器阶段信息
    vertShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO; // 结构体类型
    vertShaderStageInfo.stage = VK_SHADER_STAGE_VERTEX_BIT; // 着色器阶段, 顶点着色器
    vertShaderStageInfo.module = vertShaderModule; // 着色器模块, 顶点着色器模块
    vertShaderStageInfo.pName = "main"; // 着色器入口点, main

    VkPipelineShaderStageCreateInfo fragShaderStageInfo{}; // 片元着色器阶段信息
    fragShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO; // 结构体类型
    fragShaderStageInfo.stage = VK_SHADER_STAGE_FRAGMENT_BIT; // 着色器阶段, 片元着色器
    fragShaderStageInfo.module = fragShaderModule; // 着色器模块, 片元着色器模块
    fragShaderStageInfo.pName = "main"; // 着色器入口点, main

    // typedef struct VkPipelineShaderStageCreateInfo {
    //     VkStructureType                     sType;   // 结构体类型
    //     const void*                         pNext;   // 扩展链指针，常为 nullptr
    //     VkPipelineShaderStageCreateFlags    flags;   // 创建标志，常为 0
    //     VkShaderStageFlagBits               stage;   // 着色器阶段
    //     VkShaderModule                      module;  // 着色器模块
    //     const char*                         pName;   // 着色器入口点
    //     const VkSpecializationInfo*         pSpecializationInfo; // 着色器特殊化信息，常为 nullptr
    // } VkPipelineShaderStageCreateInfo;
    VkPipelineShaderStageCreateInfo shaderStages[] = {
        vertShaderStageInfo, 
        fragShaderStageInfo
    }; // 着色器阶段信息数组

    // typedef struct VkVertexInputBindingDescription {
    //     uint32_t             binding;    // 绑定点
    //     uint32_t             stride;     // 顶点数据的步长
    //     VkVertexInputRate    inputRate;  // 顶点输入速率
    // } VkVertexInputBindingDescription;   // 顶点绑定描述，每个元素描述一个顶点缓冲区绑定
    // typedef struct VkVertexInputAttributeDescription {
    //     uint32_t    location;            // 着色器中的位置
    //     uint32_t    binding;             // 绑定点
    //     VkFormat    format;              // 数据格式
    //     uint32_t    offset;              // 数据偏移
    // } VkVertexInputAttributeDescription; // 顶点属性描述, 如位置、颜色、法线、UV
    // typedef struct VkPipelineVertexInputStateCreateInfo {
    //     VkStructureType                             sType;   // 结构体类型
    //     const void*                                 pNext;   // 扩展链指针，常为 nullptr
    //     VkPipelineVertexInputStateCreateFlags       flags;   // 创建标志，常为 0
    //     uint32_t                                    vertexBindingDescriptionCount;   // 顶点绑定描述数量
    //     const VkVertexInputBindingDescription*      pVertexBindingDescriptions;      // 顶点绑定描述数组
    //     uint32_t                                    vertexAttributeDescriptionCount; // 顶点属性描述数量
    //     const VkVertexInputAttributeDescription*    pVertexAttributeDescriptions;    // 顶点属性描述数组
    // } VkPipelineVertexInputStateCreateInfo;
    VkPipelineVertexInputStateCreateInfo vertexInputInfo{}; // 顶点输入信息
    vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO; // 结构体类型
    vertexInputInfo.vertexBindingDescriptionCount = 
        static_cast<uint32_t>(config.bindingDescriptions.size()); // 顶点绑定描述数量
    vertexInputInfo.pVertexBindingDescriptions = 
        config.bindingDescriptions.data(); // 顶点绑定描述数组
    vertexInputInfo.vertexAttributeDescriptionCount = 
        static_cast<uint32_t>(config.attributeDescriptions.size()); // 顶点属性描述数量
    vertexInputInfo.pVertexAttributeDescriptions = 
        config.attributeDescriptions.data(); // 顶点属性描述数组

    // typedef struct VkPipelineInputAssemblyStateCreateInfo {
    //     VkStructureType                            sType;    // 结构体类型
    //     const void*                                pNext;    // 扩展链指针，常为 nullptr
    //     VkPipelineInputAssemblyStateCreateFlags    flags;    // 创建标志，常为 0
    //     VkPrimitiveTopology                        topology; // 图元拓扑
    //     VkBool32                                   primitiveRestartEnable; // 图元重启
    // } VkPipelineInputAssemblyStateCreateInfo; // 输入装配信息
    VkPipelineInputAssemblyStateCreateInfo inputAssembly{}; // 输入装配信息
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO; // 结构体类型
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST; // 图元拓扑, 三角形列表
    inputAssembly.primitiveRestartEnable = VK_FALSE; // 图元重启, 禁用

    VkPipelineViewportStateCreateInfo viewportState{}; // 视口状态信息
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO; // 结构体类型
    viewportState.viewportCount = 1; // 视口数量, 一个
    viewportState.pViewports = nullptr; // 视口数组, 无
    viewportState.scissorCount = 1; // 裁剪区域数量, 一个
    viewportState.pScissors = nullptr; // 裁剪区域数组, 无

    VkPipelineRasterizationStateCreateInfo rasterizer{}; // 光栅化信息
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO; // 结构体类型
    rasterizer.depthClampEnable = VK_FALSE; // 深度剪裁, 禁用
    rasterizer.rasterizerDiscardEnable = VK_FALSE; // 光栅化丢弃, 禁用
    rasterizer.polygonMode = config.polygonMode; // 多边形模式
    rasterizer.lineWidth = 1.0f; // 线宽, 1.0
    rasterizer.cullMode = config.cullMode; // 剔除模式, GPU 自动剔除背面三角形
    rasterizer.frontFace = config.frontFace; // 正面朝向, 定义了哪个方向是“正面”
    rasterizer.depthBiasEnable = VK_FALSE; // 深度偏移, 禁用

    VkPipelineMultisampleStateCreateInfo multisampling{}; // 多重采样信息
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO; // 结构体类型
    multisampling.sampleShadingEnable = VK_FALSE; // 样本着色, 禁用
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT; // 样本数, 1x

    VkPipelineColorBlendAttachmentState colorBlendAttachment{}; // 颜色混合附着描述
    colorBlendAttachment.colorWriteMask = 
        VK_COLOR_COMPONENT_R_BIT | 
        VK_COLOR_COMPONENT_G_BIT | 
        VK_COLOR_COMPONENT_B_BIT | 
        VK_COLOR_COMPONENT_A_BIT; // 颜色写入掩码, RGBA
    colorBlendAttachment.blendEnable = VK_FALSE; // 混合启用, 禁用

    VkPipelineDepthStencilStateCreateInfo depthStencil{}; // 深度模板信息
    depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO; // 结构体类型
    depthStencil.depthTestEnable = config.depthTestEnable; // 深度测试启用
    depthStencil.depthWriteEnable = config.depthWriteEnable; // 深度写入启用
    depthStencil.depthCompareOp = config.depthCompareOp; // 深度比较操作
    depthStencil.depthBoundsTestEnable = VK_FALSE; // 深度范围测试启用, 禁用
    depthStencil.stencilTestEnable = VK_FALSE; // 模板测试启用, 禁用
    
    VkPipelineColorBlendStateCreateInfo colorBlending{}; // 颜色混合信息
    colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO; // 结构体类型
    colorBlending.logicOpEnable = VK_FALSE; // 逻辑操作启用, 禁用
    colorBlending.logicOp = VK_LOGIC_OP_COPY; // 逻辑操作, 复制
    colorBlending.attachmentCount = 1; // 附着数量, 一个
    colorBlending.pAttachments = &colorBlendAttachment; // 附着描述, 上面的 colorBlendAttachment

    std::vector<VkDynamicState> dynamicStates = {
        VK_DYNAMIC_STATE_VIEWPORT, // 视口
        VK_DYNAMIC_STATE_SCISSOR // 裁剪区域
    };

    VkPipelineDynamicStateCreateInfo dynamicState{}; // 动态状态信息
    dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO; // 结构体类型
    dynamicState.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size()); // 动态状态数量
    dynamicState.pDynamicStates = dynamicStates.data(); // 动态状态数组

    VkPipelineLayoutCreateInfo pipelineLayoutInfo{}; // 管线布局信息
    // Pipeline layout 负责的是非顶点流数据——比如：
    // Uniform Buffer（MVP 矩阵、时间等）、纹理采样器、Storage Buffer、Push Constants
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO; // 结构体类型
    pipelineLayoutInfo.setLayoutCount = 
        static_cast<uint32_t>(config.descriptorSetLayouts.size()); // 布局数量
    pipelineLayoutInfo.pSetLayouts = 
        config.descriptorSetLayouts.data(); // 布局数组
    pipelineLayoutInfo.pushConstantRangeCount = 0; // 推送常量范围数量, 无

    if(vkCreatePipelineLayout(m_Device, &pipelineLayoutInfo, nullptr, &m_PipelineLayout) != VK_SUCCESS){ // 创建管线布局
        throw std::runtime_error("Failed to create pipeline layout!"); // 失败
    }

    // typedef struct VkGraphicsPipelineCreateInfo {
    //     VkStructureType                                  sType;                  // 结构体类型
    //     const void*                                      pNext;                  // 扩展链指针，常为 nullptr
    //     VkPipelineCreateFlags                            flags;                  // 创建标志，常为 0
    //     uint32_t                                         stageCount;             // 着色器阶段数量
    //     const VkPipelineShaderStageCreateInfo*           pStages;                // 着色器阶段数组
    //     const VkPipelineVertexInputStateCreateInfo*      pVertexInputState;      // 顶点输入信息
    //     const VkPipelineInputAssemblyStateCreateInfo*    pInputAssemblyState;    // 输入装配信息
    //     const VkPipelineTessellationStateCreateInfo*     pTessellationState;     // 细分控制信息
    //     const VkPipelineViewportStateCreateInfo*         pViewportState;         // 视口状态信息
    //     const VkPipelineRasterizationStateCreateInfo*    pRasterizationState;    // 光栅化信息
    //     const VkPipelineMultisampleStateCreateInfo*      pMultisampleState;      // 多重采样信息
    //     const VkPipelineDepthStencilStateCreateInfo*     pDepthStencilState;     // 深度模板状态信息
    //     const VkPipelineColorBlendStateCreateInfo*       pColorBlendState;       // 颜色混合信息
    //     const VkPipelineDynamicStateCreateInfo*          pDynamicState;          // 动态状态信息
    //     VkPipelineLayout                                 layout;                 // 管线布局
    //     VkRenderPass                                     renderPass;             // 渲染通道
    //     uint32_t                                         subpass;                // 子通道
    //     VkPipeline                                       basePipelineHandle;     // 基础管线
    //     int32_t                                          basePipelineIndex;      // 基础管线索引
    // } VkGraphicsPipelineCreateInfo;  // 图形管线创建信息
    VkGraphicsPipelineCreateInfo pipelineInfo{}; // 管线信息
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO; // 结构体类型
    pipelineInfo.stageCount = 2; // 着色器阶段数量, 两个
    pipelineInfo.pStages = shaderStages; // 着色器阶段数组, 上面的 shaderStages
    pipelineInfo.pVertexInputState = &vertexInputInfo; // 顶点输入信息, 上面的 vertexInputInfo
    pipelineInfo.pInputAssemblyState = &inputAssembly; // 输入装配信息, 上面的 inputAssembly, 指定顶点以什么方式连接
    pipelineInfo.pViewportState = &viewportState; // 视口状态信息, 上面的 viewportState
    pipelineInfo.pRasterizationState = &rasterizer; // 光栅化信息, 上面的 rasterizer
    pipelineInfo.pMultisampleState = &multisampling; // 多重采样信息, 上面的 multisampling, 多重采样抗锯齿（MSAA）
    pipelineInfo.pDepthStencilState = &depthStencil; // 深度模板状态信息, 上面的 depthStencil
    pipelineInfo.pColorBlendState = &colorBlending; // 颜色混合信息, 上面的 colorBlending, 片段着色器输出如何与帧缓冲现有颜色混合
    pipelineInfo.pDynamicState = &dynamicState; // 动态状态信息, 上面的 dynamicState, 指定哪些状态可以在不重建管线的情况下动态更改
    pipelineInfo.layout = m_PipelineLayout; // 管线布局, 上面的 m_PipelineLayout, 管线布局决定了着色器如何访问资源
    pipelineInfo.renderPass = renderPass; // 渲染通道, 上面的 renderPass, 渲染通道决定了管线如何与帧缓冲交互
    pipelineInfo.subpass = 0; // 子通道, 0
    pipelineInfo.basePipelineHandle = VK_NULL_HANDLE; // 基础管线, 无
    pipelineInfo.basePipelineIndex = -1; // 基础管线索引, -1

    if(vkCreateGraphicsPipelines(m_Device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &m_Pipeline) != VK_SUCCESS){ // 创建管线
        throw std::runtime_error("Failed to create graphics pipeline!"); // 失败    
    }

    std::cout << "Created graphics pipeline: OK\n"; // 成功
}
}