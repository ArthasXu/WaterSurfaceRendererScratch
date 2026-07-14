#pragma once

#include "core/Application.h"

#include "scene/Camera.h"
#include "scene/water/common/FFTResourceContract.h"
#include "scene/water/common/Stage6OceanConfig.h"
#include "scene/water/render/DynamicImage2D.h"
#include "scene/water/render/WaterGrid.h"
#include "scene/water/render/WaterSampler.h"
#include "scene/water/sources/WSTessendorfCascadesCPU.h"

#include "vulkan/Buffer.h"
#include "vulkan/Descriptors.h"
#include "vulkan/Pipeline.h"

#include <glm/glm.hpp>

#include <cstdint>
#include <memory>
#include <vector>
#include <array>

// 将波浪数据从 “CPU 直接写入顶点缓冲” 切换为 “CPU 计算位移图 → 以纹理形式上传 → 着色器采样后偏移顶点”

// Stage 5（Gerstner）：在 CPU 端遍历每个顶点，调用 Gerstner 采样函数，计算出变形后的位置和法线，
// 存入 m_DeformedVertices，再通过 Map/Unmap 更新到动态顶点缓冲区。所以需要一个 CPU 端的中间数组来存储变形结果

// Stage 6（CPU FFT）：采用了“静态基础网格 + 着色器采样位移图”的方案。
// WaterGrid 只存储一份不变的静态网格（未扰动的平面），并上传到 DEVICE_LOCAL 顶点缓冲。
// 每一帧，CPU 将 Tessendorf FFT 计算得到的位移场和法线辅助场以纹理形式上传（通过 DynamicImage2D），
// 然后在顶点着色器里采样这些纹理，直接在 GPU 上完成顶点偏移和法线重构。
// 因此，CPU 端不再需要存储全量的变形顶点数组，变形计算完全发生在 GPU 上

// 这种改变带来了显著优势：避免了每帧将大量顶点从 CPU 搬到 GPU 的带宽瓶颈，
// 并且为后续的 GPU Compute FFT 和 Cascade 多层叠加奠定了架构基础
class Stage6CPUFFTApp : public core::Application
{
protected:
    void Start() override;
    void Update(core::Timestep timestep) override;
    void PrepareFrame(uint32_t frameIndex, uint32_t imageIndex) override;
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
        glm::ivec4 debug{0, 0, 0, 0};
    };

    struct CPUFFTFrameResources
    {
        std::array<std::unique_ptr<vkp::Buffer>, water::kMaxFFTCascades> displacementStaging;
        std::array<std::unique_ptr<vkp::Buffer>, water::kMaxFFTCascades> normalAuxStaging;

        std::array<std::unique_ptr<water::DynamicImage2D>, water::kMaxFFTCascades> displacementImage;
        std::array<std::unique_ptr<water::DynamicImage2D>, water::kMaxFFTCascades> normalAuxImage;
    };

private:
    void CreateDescriptorSetLayout();
    void CreatePipelines();
    void CreateWaterGrid();
    void CreateTessendorfSource();
    void CreateSamplers();
    void CreateUniformBuffers();
    void CreateCPUFFTFrameResources();
    void CreateDescriptorPool();
    void CreateDescriptorSets();

    void UpdateCamera(float deltaTime);
    void UpdateCameraUniformBuffer(uint32_t frameIndex);
    void UpdateWaterParamsUniformBuffer(uint32_t frameIndex);
    void UpdateWindowTitle();

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
    bool m_Paused = false;
    bool m_StepOnce = false;
    int m_DebugMode = 0;

    // 配置参数：决定 FFT 网格大小和物理范围，供创建 WSTessendorfCPU 和分配纹理使用。
    water::Stage6OceanConfig m_OceanConfig =
        water::MakeStage6ReferenceOceanConfig();

    uint32_t m_FFTResolution =
        m_OceanConfig.spectrum.resolution;

    // 波浪模拟源：每帧执行频谱演化与 IFFT，生成空间域的位移、法线等数据
    std::unique_ptr<water::WSTessendorfCascadesCPU> m_Cascades;
    std::unique_ptr<water::WaterGrid> m_WaterGrid;

    // 采样器：控制着色器如何读取位移图（重复寻址、最近点过滤等）。
    std::unique_ptr<water::WaterSampler> m_FFTSampler;

    std::unique_ptr<vkp::Pipeline> m_SolidPipeline;
    std::unique_ptr<vkp::Pipeline> m_WireframePipeline;

    std::unique_ptr<vkp::DescriptorSetLayout> m_DescriptorSetLayout;
    std::unique_ptr<vkp::DescriptorPool> m_DescriptorPool;

    std::vector<std::unique_ptr<vkp::Buffer>> m_CameraUniformBuffers;
    // 水体参数 UBO：将 FFT 分辨率、补丁长度、choppy 强度等参数传给着色器。
    std::vector<std::unique_ptr<vkp::Buffer>> m_WaterParamsUniformBuffers;
    // 每帧动态资源：为每个飞行帧提供独立的 staging buffer 与 DynamicImage2D，保证 CPU 上传与 GPU 消费不冲突。
    std::vector<CPUFFTFrameResources> m_CPUFFTFrames;

    std::vector<VkDescriptorSet> m_DescriptorSets;
};