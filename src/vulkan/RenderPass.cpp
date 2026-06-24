#include "RenderPass.h"

#include <iostream>
#include <stdexcept>

namespace vkp
{
RenderPass::RenderPass(VkDevice device, VkFormat colorFormat)
    : m_Device(device)
{
    createRenderPass(colorFormat);
}

RenderPass::~RenderPass()
{
    if(m_RenderPass != VK_NULL_HANDLE){
        vkDestroyRenderPass(m_Device, m_RenderPass, nullptr); // 销毁渲染通道
    }
}

RenderPass::operator VkRenderPass() const
{
    return m_RenderPass;
}

VkRenderPass RenderPass::GetHandle() const
{
    return m_RenderPass;
}

void RenderPass::createRenderPass(VkFormat colorFormat){
    // VkRenderPassCreateInfo
    // ├── pAttachments → [ VkAttachmentDescription ]   (第 0 个：颜色附件)
    // │
    // ├── pSubpasses → [ VkSubpassDescription ]
    // │                 └── pColorAttachments → [ VkAttachmentReference ]
    // │                         └── attachment = 0     (指向 pAttachments[0])
    // │                             layout = COLOR_ATTACHMENT_OPTIMAL
    // │
    // └── pDependencies → [ VkSubpassDependency ]
    //                     srcSubpass = EXTERNAL
    //                     dstSubpass = 0
    //                     (确保外部操作完成后才开始子过程 0)
    // typedef struct VkAttachmentDescription {
    //     VkAttachmentDescriptionFlags    flags;               // 附件标志
    //     VkFormat                        format;              // 附件格式
    //     VkSampleCountFlagBits           samples;             // 附件样本数
    //     VkAttachmentLoadOp              loadOp;              // 附件加载操作
    //     VkAttachmentStoreOp             storeOp;             // 附件存储操作
    //     VkAttachmentLoadOp              stencilLoadOp;       // 附件深度/模板加载操作
    //     VkAttachmentStoreOp             stencilStoreOp;      // 附件深度/模板存储操作
    //     VkImageLayout                   initialLayout;       // 附件初始布局
    //     VkImageLayout                   finalLayout;         // 附件最终布局
    // } VkAttachmentDescription; // 附件描述
    VkAttachmentDescription colorAttachment{}; // 颜色附件描述, 定义“有哪些图像资源会被这次渲染用到”，以及它们的格式、清屏/保存策略、初始/最终 layout
    colorAttachment.format = colorFormat; // 颜色附件格式, 使用交换链的格式
    colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT; // 颜色附件样本数, 无多重采样（1x）
    colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR; // 颜色附件加载操作, 开始时清除图像
    colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE; // 颜色附件存储操作, 结束时保存结果
    colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE; // 颜色附件深度/模板加载操作, 不用模板缓冲
    colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE; // 颜色附件深度/模板存储操作
    colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED; // 颜色附件初始布局, 开始前布局无所谓
    colorAttachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR; // 颜色附件最终布局, 结束后适合呈现

    VkAttachmentReference colorAttachmentRef{}; // 颜色附件引用, “子过程要用到 pAttachments 中的哪一个附件，以什么布局使用”
    colorAttachmentRef.attachment = 0; // 颜色附件索引, 引用第 0 号附着（即上面的 colorAttachment）
    colorAttachmentRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL; // 颜色附件布局, 子过程内最佳布局

    VkSubpassDescription subpass{}; // 子过程描述, 定义“一次具体绘制步骤怎么使用 attachment”。这里 graphics pipeline 写入 attachment 0
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS; // 子过程绑定点, 图形管线
    subpass.colorAttachmentCount = 1; // 颜色附件数量, 一个
    subpass.pColorAttachments = &colorAttachmentRef; // 颜色附件引用, 上面的 colorAttachmentRef

    VkSubpassDependency dependency{}; // 子过程依赖, 定义“外部操作和 subpass 之间的同步与 layout transition”。这里保证写 color attachment 前，图像已进入正确状态
    dependency.srcSubpass = VK_SUBPASS_EXTERNAL; // 源子过程, 外部（即没有依赖的子过程）
    dependency.dstSubpass = 0; // 目标子过程, 第 0 号子过程
    dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT; // 源阶段, 颜色附着输出
    dependency.srcAccessMask = 0; // 源访问, 无
    dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT; // 目标阶段, 颜色附着输出
    dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT; // 目标访问, 颜色附着写入

    VkRenderPassCreateInfo renderPassInfo{}; // 呈现过程创建信息, 把所有部分打包成一个完整的渲染通道对象
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO; // 结构体类型
    renderPassInfo.attachmentCount = 1; // 附件数量, 一个
    renderPassInfo.pAttachments = &colorAttachment; // 附件描述, 上面的 colorAttachment
    renderPassInfo.subpassCount = 1; // 子过程数量, 一个
    renderPassInfo.pSubpasses = &subpass; // 子过程描述, 上面的 subpass
    renderPassInfo.dependencyCount = 1; // 子过程依赖数量, 一个
    renderPassInfo.pDependencies = &dependency; // 子过程依赖, 上面的 dependency

    if(vkCreateRenderPass(m_Device, &renderPassInfo, nullptr, &m_RenderPass) != VK_SUCCESS){ // 创建呈现过程
        throw std::runtime_error("Failed to create render pass!"); // 失败
    }

    std::cout << "Render pass created: OK\n"; // 成功
}
}