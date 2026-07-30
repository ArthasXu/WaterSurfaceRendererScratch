#pragma once

// ===== River field baker GUI app =====
// A minimal core::Application (window + Vulkan + ImGui, no water rendering)
// that lets you edit the control points / spline smoothing / field config /
// shore params, then bake the 4 fields and write the .bin bundle the runtime
// app reads. Replaces the old headless CLI baker.

#include "core/Application.h"

#include "scene/water/river/RiverFieldTypes.h"
#include "scene/water/river/ShoreFieldTypes.h"

#include <vulkan/vulkan.h>

#include <string>
#include <vector>

class RiverFieldBakerApp : public core::Application
{
protected:
    void Start() override;
    void Update(core::Timestep timestep) override;
    void Render(VkCommandBuffer commandBuffer, uint32_t imageIndex) override;
    void ShutdownApp() override;

private:
    void SetupGui();
    void DrawGui();
    void BakeAndSave();

    // 河流中轴线控制点（GUI 可增删改）
    std::vector<water::RiverControlPoint> m_ControlPoints;
    // spline.Build 每段插值采样数，越大曲线越平滑，烘焙越慢
    int m_SamplesPerSegment = 24;
    // 场配置：世界范围/分辨率/河岸淡出
    water::RiverFieldConfig m_FieldConfig{};
    // 岸线场参数：湿润带/沙滩/岸上地形
    water::ShoreFieldParams m_ShoreParams{};
    // 输出 .bin 路径
    char m_OutputPath[256] = "assets/river/river_field.bin";

    VkDescriptorPool m_GuiDescriptorPool = VK_NULL_HANDLE;
    bool m_GuiEnabled = true;
    std::string m_StatusText = "Ready.";
};
