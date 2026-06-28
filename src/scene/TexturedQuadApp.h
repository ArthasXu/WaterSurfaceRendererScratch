#pragma once

#include "core/Application.h"

#include "scene/Camera.h"

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

private:
    scene::Camera m_Camera; // 相机

    bool m_Keys[1024] = {false}; // 键盘按键状态
    bool m_FirstMouse = true; // 是否是第一次鼠标移动
    double m_LastMouseX = 0.0; // 上一次鼠标位置
    double m_LastMouseY = 0.0; // 上一次鼠标位置
    bool m_CameraControlEnabled = true; // 是否启用相机控制

    float m_Time = 0.0f; // 时间
    float m_TitleUpdateTimer = 0.0f; // 标题更新计时器
};