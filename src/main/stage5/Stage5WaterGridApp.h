#pragma once

#include "core/Application.h"

#include "scene/Camera.h"
#include "scene/water/common/WaterSurfaceComposer.h"
#include "scene/water/render/WaterGrid.h"
#include "scene/water/sources/WSGerstnerCPU.h"

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
    void CreateGerstnerSource(); // 创建 Gerstner 源
    void CreateUniformBuffers();
    void CreateDescriptorPool();
    void CreateDescriptorSets();

    void UpdateCameraUniformBuffer();
    void UpdateWaterSimulation(float deltaTime); // 更新水模拟
    void UpdateWindowTitle(); // 更新窗口标题

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

    bool m_Paused = false;
    bool m_StepOnce = false;

    water::WaterSurfaceComposer m_Composer; // 水面合成器
    std::shared_ptr<water::WSGerstnerCPU> m_GerstnerSource; // 共享指针持有 Gerstner 波浪计算器
    // 它可以被多个对象（例如本应用和 WaterSurfaceComposer）共同引用
    // 每帧通过 Update(deltaTime) 驱动内部时间，随后调用 Sample(worldXZ) 获取任意世界坐标点的高度、水平位移和斜率

    std::unique_ptr<water::WaterGrid> m_WaterGrid; // 水网格
    std::vector<water::WaterVertex> m_DeformedVertices; // 变形后的顶点
    // 每一帧，CPU 根据当前波浪状态计算所有顶点的变形位置、法线等，结果填入这个数组，
    // 随后通过 Map/Unmap 或 CopyToMapped 更新到 WaterGrid 的顶点缓冲区中，供 GPU 渲染使用

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