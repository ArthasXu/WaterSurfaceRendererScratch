#pragma once

#include "core/Application.h"

#include "scene/Camera.h"
#include "scene/water/common/FFTResourceContract.h"
#include "scene/water/common/Stage6OceanConfig.h"
#include "scene/water/render/WaterGrid.h"
#include "scene/water/render/WaterSampler.h"
#include "scene/water/gpu/WSTessendorfGPU.h"
#include "scene/water/bore/BoreFrontTypes.h"
#include "scene/water/bore/FrontParameterLUT.h"
#include "scene/water/common/BoreResourceContract.h"
#include "scene/water/render/StaticFloatTexture2D.h"
#include "scene/water/bore/BoreProfileTypes.h"
#include "scene/water/bore/BoreProfileResourceContract.h"
#include "scene/water/render/StaticDataTexture2D.h"
#include "scene/water/foam/FoamResourceContract.h"
#include "scene/water/foam/FoamTypes.h"
#include "scene/water/render/ComputeImage2D.h"
#include "scene/water/gpu/ComputePipeline.h"
#include "scene/water/material/WaterMaterialResourceContract.h"
#include "scene/water/lod/WaterPatchMesh.h"
#include "scene/water/lod/WaterQuadtree.h"

#include "scene/water/river/RiverSpline.h"
#include "scene/water/river/RiverField.h"
#include "scene/water/river/RiverFieldBaker.h"
#include "scene/water/river/RiverResourceContract.h"

#include "vulkan/Buffer.h"
#include "vulkan/Descriptors.h"
#include "vulkan/Pipeline.h"

#include <vulkan/vulkan.h>

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
class Stage10RiverWaterApp : public core::Application
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
        glm::vec4 cameraWorldPosition{0.0f};   // fragment 需要真实相机世界坐标
        glm::ivec4 debug{0, 0, 0, 0};
    };

    struct BoreProfileGuiParams
    {
        float waterRiseHeight = 8.0f;
        float riseWidth = 8.0f;
        float fixedPhase = 0.60f;
        float profileWidthScale = 0.0f;
        float globalAmplitude = 1.55f;
        float forwardScale = 3.0f;
        float upwardScale = 1.6f;
        float activeRegionMask = 1.0f;
        glm::vec4 suppression{0.20f, 0.35f, 0.80f, 0.0f};
    };

    struct FoamGuiParams
    {
        float animationCycle = 4.0f;
        float detailWorldScale = 0.08f;
        glm::vec4 sourceStrength{1.0f, 0.35f, 0.45f, 0.75f};
        glm::vec4 thresholds{0.28f, 0.70f, 0.04f, 0.30f};
        glm::vec4 appearance{0.32f, 0.15f, 0.35f, 0.25f};
        float stateGain = 1.8f;
        float stateDecay = 0.45f;
        float stateDiffusion = 0.02f;
        float stateEnabled = 0.0f;
        glm::vec4 domain{-128.0f, -128.0f, 256.0f, 256.0f};
        bool solverEnabled = true;
    };

    struct WaterMaterialGuiParams
    {
        glm::vec4 shallowColor{0.10f, 0.32f, 0.34f, 1.0f};
        glm::vec4 deepColor{0.01f, 0.08f, 0.13f, 1.0f};
        glm::vec4 sedimentColor{0.42f, 0.30f, 0.16f, 1.0f};
        glm::vec4 opticalParams{0.0204f, 0.35f, 0.45f, 0.35f};
        glm::vec3 sunDirection{-0.35f, 0.85f, 0.25f};
        float specularStrength = 1.2f;
        glm::vec4 fogParams{120.0f, 700.0f, 0.4f, 0.0f};
    };

    struct QuadtreeGuiParams
    {
        glm::vec2 rootCenter{0.0f};
        float rootSize = 2048.0f;
        int maxLevel = 6;
        int patchCellCount = 32;
        float fovYDegrees = 45.0f;
        float splitPixels = 9.0f;
        float mergePixels = 6.0f;
        float minY = -15.0f;
        float maxY = 20.0f;
    };

private:
    void CreateDescriptorSetLayout();          // 创建几何描述符集布局（set=0），定义 FFT/Bore/UBO 等资源绑定
    void CreatePipelines();                    // 创建图形管线（实体+线框），绑定两个描述符集布局（set0/set1）
    void CreateWaterGrid();                    // 创建静态水面网格（128×128顶点），上传到设备本地内存
    void CreateWaterPatch();

    void CreateRiverResources();

    void UpdateRiverFieldUniformBuffer(
        uint32_t frameIndex
    );

    bool ClassifyRiverTile(
        water::WaterTile& tile
    ) const;

    uint32_t GetRiverRequiredLevel(
        const water::WaterTile& tile
    ) const;

    void UpdateQuadtree();
    void CreateTileInstanceBuffers();
    void UpdateTileInstanceBuffer(
        uint32_t frameIndex
    );                                         // 不再每 Tile push constant。先上传实例 buffer，再一次绘制
    void DrawQuadtreeTiles(
        VkCommandBuffer commandBuffer
    );
    void CreateSamplers();                     // 为 FFT、LUT、Profile 和泡沫纹理分别创建对应寻址模式的采样器
    void CreateBoreFrontResources();           // 创建涌潮波前 GPU 资源（Front LUT 纹理、BoreFrontUBO 缓冲）
    void CreateBoreProfileResources();         // 创建涌潮剖面 GPU 资源（位移/导数纹理、BoreProfileUBO 缓冲）
    void CreateFoamResources();                // 创建泡沫 GPU 资源（细节纹理、状态泡沫图像、源/速度纹理）
    void CreateAppearanceDescriptorSetLayout(); // 创建外观描述符集布局（set=1），定义泡沫和材质资源绑定
    void CreateAppearanceDescriptorPool();     // 创建外观描述符池，为 set=1 每帧描述符集分配空间
    void CreateAppearanceDescriptorSets();     // 创建每帧外观描述符集，绑定泡沫参数 UBO 与泡沫纹理
    void UpdateFoamParamsUniformBuffer(uint32_t frameIndex); // 更新泡沫参数 UBO（时间、权重、阈值等）
    void CreateUniformBuffers();               // 创建所有每帧 UBO（相机、水体参数、BoreFront、BoreProfile、泡沫参数）
    void CreateGPUFFTSource();                 // 创建 GPU FFT 波浪模拟器（三层 Cascade + Compute Pipeline）
    void CreateDescriptorPool();               // 创建几何描述符池（set=0），为每帧几何描述符集分配空间
    void CreateDescriptorSets();               // 创建每帧几何描述符集，绑定 CameraUBO、FFT 纹理、Bore 纹理

    void CreateFoamStateResources();
    void CreateFoamComputeDescriptorSetLayouts();
    void CreateFoamComputePipelines();
    void CreateFoamComputeDescriptorPool();
    void CreateFoamComputeDescriptorSets();
    void InitializeFoamStateImages();
    void RecordFoamSimulation(VkCommandBuffer commandBuffer, uint32_t frameIndex);
    
    void UpdateCamera(float deltaTime);        // 处理键盘输入（WASD/Space/LCtrl）移动相机
    void UpdateCameraUniformBuffer(uint32_t frameIndex); // 写入当前帧的 CameraUBO（MVP 矩阵 + 调试模式）
    void UpdateWaterParamsUniformBuffer(uint32_t frameIndex); // 写入当前帧的 WaterParamsUBO（patch 长度、振幅、时间）
    void UpdateBoreFrontUniformBuffer(uint32_t frameIndex); // 写入当前帧的 BoreFrontUBO（波前位置、方向、时间）
    void UpdateBoreProfileUniformBuffer(uint32_t frameIndex); // 写入当前帧的 BoreProfileUBO（剖面尺寸、动画时间）
    void UpdateFoamSimulationUniformBuffer(uint32_t frameIndex); // 写入当前帧的 FoamSimulationUBO（时间、权重、阈值等）
    void UpdateWaterMaterialUniformBuffer(uint32_t frameIndex); // 写入当前帧的 WaterMaterialUBO（颜色、粗糙度等）

    void SetupGui();
    void DrawGui();
    void RebuildQuadtreeFromGui();
    void ResetBoreEvent();

    void UpdateWindowTitle();                  // 每 0.5s 更新窗口标题，显示相机位置、调试模式、线框/暂停状态

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

    bool m_GuiEnabled = true;
    VkDescriptorPool m_GuiDescriptorPool = VK_NULL_HANDLE;
    BoreProfileGuiParams m_BoreProfileGui{};
    FoamGuiParams m_FoamGui{};
    WaterMaterialGuiParams m_WaterMaterialGui{};
    QuadtreeGuiParams m_QuadtreeGui{};

    // 配置参数：决定 FFT 网格大小和物理范围，供创建 WSTessendorfCPU 和分配纹理使用。
    water::Stage6OceanConfig m_OceanConfig =
        water::MakeStage6ReferenceOceanConfig();

    uint32_t m_FFTResolution =
        m_OceanConfig.spectrum.resolution;

    float m_LastSimulationDeltaTime = 0.0f;
    float m_BoreTime = 0.0f;
    float m_LastBoreDeltaTime = 0.0f;

    bool m_BorePaused = false;
    bool m_BoreEnabled = true;
    bool m_BoreUseLUT = false;
    bool m_BoreDebugRidgeEnabled = false;

    water::BoreFrontParams m_BoreFrontParams{};
    water::FrontLUTData m_FrontLUT{};

    // Wave Profile
    water::BoreWaveProfileConfig m_BoreProfileConfig{};
    water::BoreWaveProfileData m_BoreProfileData{};

    float m_ProfileTime = 0.0f;
    float m_EventRepeatDuration = 30.0f;

    water::BoreProfileAnimationMode m_ProfileMode =
        water::BoreProfileAnimationMode::OneShot;

    bool m_ProfilePaused = true; // 固定 Profile 在成熟阶段
    bool m_ProfileEnabled = true;
    bool m_FFTEnabled = true;
    bool m_AutoRepeatEvent = false;

    // 波浪模拟源：每帧执行频谱演化与 IFFT，生成空间域的位移、法线等数据
    std::unique_ptr<water::WSTessendorfGPU> m_TessendorfGPU;
    std::unique_ptr<water::WaterGrid> m_WaterGrid;
    std::unique_ptr<water::WaterPatchMesh> m_WaterPatchMesh;
    std::unique_ptr<water::WaterQuadtree> m_WaterQuadtree;
    std::vector<water::WaterTile> m_VisibleWaterTiles;

    // 采样器：控制着色器如何读取位移图（重复寻址、最近点过滤等）。
    std::unique_ptr<water::WaterSampler> m_FFTSampler;
    std::unique_ptr<water::WaterSampler> m_FrontLUTSampler;
    std::unique_ptr<water::WaterSampler> m_BoreProfileSampler;
    std::unique_ptr<water::WaterSampler> m_RiverSampler;
    std::unique_ptr<water::WaterSampler> m_FoamDetailSampler;
    std::unique_ptr<water::WaterSampler> m_FoamStateSampler;

    std::unique_ptr<vkp::Pipeline> m_SolidPipeline;
    std::unique_ptr<vkp::Pipeline> m_WireframePipeline;

    std::unique_ptr<vkp::DescriptorSetLayout> m_DescriptorSetLayout;
    std::unique_ptr<vkp::DescriptorPool> m_DescriptorPool;
    std::unique_ptr<vkp::DescriptorSetLayout> m_AppearanceDescriptorSetLayout;
    std::unique_ptr<vkp::DescriptorPool> m_AppearanceDescriptorPool;

    std::vector<std::unique_ptr<vkp::Buffer>> m_CameraUniformBuffers;
    // 水体参数 UBO：将 FFT 分辨率、补丁长度、choppy 强度等参数传给着色器。
    std::vector<std::unique_ptr<vkp::Buffer>> m_WaterParamsUniformBuffers;
    std::vector<std::unique_ptr<vkp::Buffer>> m_BoreFrontUniformBuffers;
    std::vector<std::unique_ptr<vkp::Buffer>> m_BoreProfileUniformBuffers;
    std::vector<std::unique_ptr<vkp::Buffer>> m_FoamParamsUniformBuffers;
    std::vector<std::unique_ptr<vkp::Buffer>> m_WaterMaterialUniformBuffers;
    std::vector<std::unique_ptr<vkp::Buffer>> m_RiverFieldUniformBuffers;
    std::vector<std::unique_ptr<vkp::Buffer>> m_TileInstanceBuffers;
    uint32_t m_MaxVisibleWaterTiles = 8192;
    uint32_t m_CurrentVisibleWaterTileCount = 0;

    std::vector<VkDescriptorSet> m_DescriptorSets;
    std::vector<VkDescriptorSet> m_AppearanceDescriptorSets;
    std::unique_ptr<water::StaticFloatTexture2D> m_FrontParameterTexture;
    std::unique_ptr<water::StaticFloatTexture2D> m_FrontDerivativeTexture;
    std::unique_ptr<water::StaticDataTexture2D> m_BoreProfileDisplacementTexture;
    std::unique_ptr<water::StaticDataTexture2D> m_BoreProfileDerivativeTexture;
    std::unique_ptr<water::StaticDataTexture2D> m_RiverFlowTexture;
    std::unique_ptr<water::StaticDataTexture2D> m_RiverCoordinateTexture;
    water::FoamDetailTextureData m_FoamDetailData{};
    std::unique_ptr<water::StaticDataTexture2D> m_FoamDetailTexture;

    // Foam
    uint32_t m_FoamResolution = 512;
    uint32_t m_CurrentFoamStateIndex = 0;
    float m_LastFoamDeltaTime = 0.0f;

    std::array<std::unique_ptr<water::ComputeImage2D>, 2> m_FoamStateImages;
    std::unique_ptr<water::ComputeImage2D> m_FoamSourceVelocityImage;

    std::vector<std::unique_ptr<vkp::Buffer>> m_FoamSimulationUniformBuffers;

    std::unique_ptr<vkp::DescriptorSetLayout> m_FoamSourceSetLayout;
    std::unique_ptr<vkp::DescriptorSetLayout> m_FoamAdvectSetLayout;
    std::unique_ptr<vkp::DescriptorPool> m_FoamComputeDescriptorPool;

    std::unique_ptr<water::ComputePipeline> m_FoamSourcePipeline;
    std::unique_ptr<water::ComputePipeline> m_FoamAdvectPipeline;

    std::vector<VkDescriptorSet> m_FoamSourceSets;
    std::vector<std::array<VkDescriptorSet, 2>> m_FoamAdvectSets;

    // River Tile
    water::RiverSpline m_RiverSpline;
    std::unique_ptr<water::RiverField> m_RiverField;
    float m_RiverLength = 0.0f;
    float m_RiverBoreCurvatureMeters = 0.0f;
};