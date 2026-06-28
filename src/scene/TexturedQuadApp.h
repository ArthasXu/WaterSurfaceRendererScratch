#pragma once

#include "core/Application.h"

#include "scene/Camera.h"

#include "vulkan/Buffer.h"
#include "vulkan/Pipeline.h"

#include <memory>
#include <cstdint>
#include <vector>

class TexturedQuadApp : public core::Application
{ // 继承自Application类
protected:
    void Start() override; // 实现纯虚函数Start
    void Update(core::Timestep timestep) override; // 实现纯虚函数Update
    void Render(VkCommandBuffer commandBuffer, uint32_t imageIndex) override; // 实现纯虚函数Render

    void OnFramebufferResize(int width, int height) override; // 实现纯虚函数OnFramebufferResize
    void OnMouseMove(double x, double y) override; // 实现纯虚函数OnMouseMove
    void OnMouseButton(int button, int action, int mods) override; // 实现纯虚函数OnMouseButton
    void OnKey(int key, int scancode, int action, int mods) override; // 实现纯虚函数OnKey

    void ShutdownApp() override; // 实现纯虚函数ShutdownApp

private:
    void CreateVertexBuffer(); // 创建顶点缓冲区，存储每个顶点的属性数据，例如位置、颜色、法线、纹理坐标等
    void CreateIndexBuffer(); // 创建索引缓冲区，存储顶点的索引值（整数），用来复用顶点
    void CreateUniformBuffers(); // 创建统一缓冲区，存储全局的变换矩阵、光照信息等
    void UpdateUniformBuffers(); // 更新统一缓冲区，将变换矩阵、光照信息等数据写入到统一缓冲区中

    void CreateGraphicsPipeline(); // 创建图形管线，用于渲染图形

private:
    scene::Camera m_Camera; // 相机

    bool m_Keys[1024] = {false}; // 键盘按键状态
    bool m_FirstMouse = true; // 是否是第一次鼠标移动
    double m_LastMouseX = 0.0; // 上一次鼠标位置
    double m_LastMouseY = 0.0; // 上一次鼠标位置
    bool m_CameraControlEnabled = true; // 是否启用相机控制

    float m_Time = 0.0f; // 时间
    float m_TitleUpdateTimer = 0.0f; // 标题更新计时器

    std::unique_ptr<vkp::Buffer> m_VertexBuffer; // 顶点缓冲区
    std::unique_ptr<vkp::Buffer> m_IndexBuffer; // 索引缓冲区
    uint32_t m_IndexCount = 0; // 索引数量

    std::unique_ptr<vkp::Pipeline> m_GraphicsPipeline; // 图形管线

    std::vector<std::unique_ptr<vkp::Buffer>> m_UniformBuffers; // UBO 统一缓冲区
};