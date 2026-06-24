#pragma once

#include <vulkan/vulkan.h>

namespace vkp
{
class RenderPass
{
public:
    RenderPass(VkDevice device, VkFormat colorFormat);
    ~RenderPass();

    RenderPass(const RenderPass&) = delete;
    RenderPass& operator=(const RenderPass&) = delete;

    operator VkRenderPass() const;
    VkRenderPass GetHandle() const;

private:
    void createRenderPass(VkFormat colorFormat); // 创建渲染通道

private:
    VkDevice m_Device = VK_NULL_HANDLE;             // 逻辑设备句柄
    VkRenderPass m_RenderPass = VK_NULL_HANDLE;     // 渲染通道句柄
};
}