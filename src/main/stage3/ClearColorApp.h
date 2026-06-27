#pragma once

#include "core/Application.h"

class ClearColorApp : public core::Application
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
    float m_Time = 0.0f; // 时间
};