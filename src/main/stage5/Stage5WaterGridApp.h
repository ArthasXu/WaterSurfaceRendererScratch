#pragma once

#include "core/Application.h"

#include "scene/Camera.h"
#include "scene/water/render/WaterGrid.h"

#include "vulkan/Buffer.h"
#include "vulkan/Descriptors.h"
#include "vulkan/Pipeline.h"

#include <glm/glm.hpp>

#include <cstdint>
#include <memory>
#include <vector>

class Stage5WaterGridApp : public core::Application
{
protected:
    void Start() override;
    void Update(core::Timestep timestep) override;
    void Render(VkCommandBuffer commandBuffer, uint32_t imageIndex) override;

    void OnFramebufferResize(int width, int height) override;
    void OnMouseMove(double x, double y) override;
    void OnMouseButton(int button, int action, int mods) override;
    void OnKey(int key, int scancode, int action, int mods) override;

    void ShutdownApp() override;

private:
    struct CameraUBO
    {
        glm::mat4 model{1.0f};
        glm::mat4 view{1.0f};
        glm::mat4 projection{1.0f};
        glm::ivec4 debug{1, 0, 0, 0};
    };

private:
    void CreateDescriptorSetLayout();
    void CreatePipelines();
    void CreateWaterGrid();
    void CreateUniformBuffers();
    void CreateDescriptorPool();
    void CreateDescriptorSets();
    void UpdateCameraUniformBuffer();

private:
    scene::Camera m_Camera;

    bool m_Keys[1024] = {};
    bool m_FirstMouse = true;
    double m_LastMouseX = 0.0;
    double m_LastMouseY = 0.0;
    bool m_CameraControlEnabled = true;

    float m_Time = 0.0f;
    float m_TitleUpdateTimer = 0.0f;

    bool m_UseWireframe = false;
    int m_DebugMode = 1;

    std::unique_ptr<water::WaterGrid> m_WaterGrid; // 水网格

    std::unique_ptr<vkp::Pipeline> m_SolidPipeline; // 实心管线
    // 以实体三角形填充水面网格。这是最终呈现给玩家的真实水面效果，配合光照、反射、折射等计算，呈现完整的水体外观
    std::unique_ptr<vkp::Pipeline> m_WireframePipeline; // 线框管线
    // 只绘制三角形边缘线。它主要用于调试和开发阶段，可以让你直观地看到水面网格的密度、顶点分布、波浪变形是否正常、有无网格撕裂等问题。
    // 比如检查 FFT 生成的波浪位移是否合理，网格是否在某些区域过度扭曲

    std::unique_ptr<vkp::DescriptorSetLayout> m_DescriptorSetLayout; // 描述符集布局
    std::unique_ptr<vkp::DescriptorPool> m_DescriptorPool; // 描述符池

    std::vector<std::unique_ptr<vkp::Buffer>> m_CameraUniformBuffers; // 相机统一缓冲区
    std::vector<VkDescriptorSet> m_DescriptorSets;  // 描述符集
};