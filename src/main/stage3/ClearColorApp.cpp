#include "main/stage3/ClearColorApp.h"

#include "core/Log.h"

#include <GLFW/glfw3.h>

#include <cmath>
#include <stdexcept>

void ClearColorApp::Start(){ // 实现纯虚函数Start
    VKP_INFO("ClearColorApp started");
}

void ClearColorApp::Update(core::Timestep timestep){ // 实现纯虚函数Update
    m_Time += timestep.GetSeconds(); // 时间累加
}

void ClearColorApp::Render(VkCommandBuffer commandBuffer, uint32_t imageIndex){ // 实现纯虚函数Render
    VkCommandBufferBeginInfo beginInfo{}; // 命令缓冲区开始信息
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO; // 结构体类型
    
    if(vkBeginCommandBuffer(commandBuffer, &beginInfo) != VK_SUCCESS){ // 开始命令缓冲区
        throw std::runtime_error("Failed to begin recording command buffer!"); // 失败
    }

    VkRenderPassBeginInfo renderPassInfo{}; // 渲染通道开始信息
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO; // 结构体类型
    renderPassInfo.renderPass = GetRenderPass(); // 获取渲染通道
    renderPassInfo.framebuffer = GetSwapChain().GetFramebuffer(imageIndex); // 获取帧缓冲区
    renderPassInfo.renderArea.offset = {0, 0}; // 渲染区域偏移
    renderPassInfo.renderArea.extent = GetSwapChain().GetExtent(); // 渲染区域大小

    float green = 0.02f + 0.2f * std::abs(std::sin(m_Time)); // 计算绿色分量

    VkClearValue clearColor = {{{0.02f, green, 0.03f, 1.0f}}}; // 清除颜色, 动态变化

    renderPassInfo.clearValueCount = 1; // 清除值数量
    renderPassInfo.pClearValues = &clearColor; // 清除值数组

    vkCmdBeginRenderPass(commandBuffer, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE); // 开始渲染通道
    vkCmdEndRenderPass(commandBuffer); // 结束渲染通道

    if(vkEndCommandBuffer(commandBuffer) != VK_SUCCESS){ // 结束命令缓冲区
        throw std::runtime_error("Failed to record command buffer!"); // 失败
    }
}

void ClearColorApp::OnFramebufferResize(int width, int height){
    core::Application::OnFramebufferResize(width, height);
    VKP_INFO("Framebuffer resized: {} x {}", width, height);
}

void ClearColorApp::OnKey(int key, int scancode, int action, int mods){
    core::Application::OnKey(key, scancode, action, mods);
}

void ClearColorApp::OnMouseMove(double x, double y){
    core::Application::OnMouseMove(x, y);
}

void ClearColorApp::OnMouseButton(int button, int action, int mods){
    core::Application::OnMouseButton(button, action, mods);
}

