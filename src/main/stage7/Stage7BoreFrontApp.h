#pragma once

#include "core/Application.h"

#include "scene/Camera.h"
#include "scene/water/common/FFTResourceContract.h"
#include "scene/water/common/Stage6OceanConfig.h"
#include "scene/water/render/WaterGrid.h"
#include "scene/water/render/WaterSampler.h"
#include "scene/water/gpu/WSTessendorfGPU.h"

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

// Stage 6（GPU FFT）：将 FFT 计算本身也迁移到 GPU 上，形成“零拷贝”数据闭环。
// CPU 只负责一次性的频谱初始化（生成 h0、波矢量、色散表）并上传到设备本地缓冲区。
// 每帧运行时，由 Compute Shader 接管整个管线：
//   1. 频谱演化着色器：根据当前时间 t，将 h0 时间演化后写入打包缓冲区
//   2. Stockham IFFT 着色器：对打包缓冲区执行二维逆 FFT（ping-pong 交替），频域数据转为空间域
//   3. 输出着色器：从打包缓冲区读取空间域数据（高度、位移、斜率、Jacobian），写入位移图和法线辅助图
// 顶点着色器直接从这些纹理采样，完成顶点偏移。
// 整个 FFT 计算过程中，数据从未离开 GPU 显存，CPU 仅需每帧传递时间 t 和少量控制参数。
// 三级 Cascade（长波、中波、短波）各自拥有独立的 ping-pong 缓冲区和输出纹理，
// 通过相同的 Compute Pipeline 调度，实现不同频段波浪的并行合成。

// 这种设计带来的优势：
//   - 彻底消除 CPU 端 FFT 的计算瓶颈（原 CPU 三层 Cascade 平均 26ms，GPU 版本可降至 1ms 以内）
//   - 减少 CPU → GPU 的数据传输：每帧仅传递时间参数和 push constants，无需上传数 MB 的位移场数据
//   - 为后续实时多层 FFT 海浪叠加（近中远波谱 + LOD 分级网格）和泡沫/白浪判据提供高性能计算基础
//   - Compute Shader 与图形管线在同一命令缓冲中执行，通过管线屏障精确同步，无需跨 API 互操作
class Stage7BoreFrontApp : public core::Application
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

private:
    void CreateDescriptorSetLayout();
    void CreatePipelines();
    void CreateWaterGrid();
    void CreateSamplers();
    void CreateUniformBuffers();
    void CreateGPUFFTSource();
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

    float m_LastSimulationDeltaTime = 0.0f;

    // 波浪模拟源：每帧执行频谱演化与 IFFT，生成空间域的位移、法线等数据
    std::unique_ptr<water::WSTessendorfGPU> m_TessendorfGPU;
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

    std::vector<VkDescriptorSet> m_DescriptorSets;
};