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
#include "scene/water/bore/BoreEventManager.h"
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
#include "scene/water/river/ProgressFieldTypes.h"
#include "scene/water/river/ShoreFieldTypes.h"

#include "scene/water/terrain/Heightmap.h"

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
class Stage12FluidFluxApp : public core::Application
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
        float waterRiseHeight = 1.8f;
        float riseWidth = 80.0f;
        float fixedPhase = 0.60f;
        float profileWidthScale = 0.0f;
        float globalAmplitude = 2.75f;
        float forwardScale = 10.0f;
        float upwardScale = 2.9f;
        float activeRegionMask = 1.0f;
        glm::vec4 suppression{0.20f, 0.35f, 0.80f, 0.0f};
    };

    struct FoamGuiParams
    {
        float animationCycle = 4.0f;
        float detailWorldScale = 0.025f; // 1/40m：泡沫细节每 40m 重复一次，避免厘米级纹理摩尔纹
        glm::vec4 sourceStrength{1.4f, 0.15f, 0.20f, 1.2f};
        glm::vec4 thresholds{0.20f, 0.58f, 0.08f, 0.35f};
        glm::vec4 appearance{0.60f, 0.15f, 0.0f, 0.25f};
        float stateGain = 1.8f;
        float stateDecay = 0.45f;
        float stateDiffusion = 0.02f;
        float stateEnabled = 0.0f;
        glm::vec4 domain{-128.0f, -128.0f, 256.0f, 256.0f};
        bool solverEnabled = true;
        float oceanFoamFadeNear = 150.0f;  // FFT 全局泡沫开始淡出的相机距离
        float oceanFoamFadeFar = 600.0f;   // FFT 全局泡沫完全消失的相机距离

        float foamShallowOffset = 0.3f;     // FF _FoamShallowOffset
        float foamShallowScale = 1.0f;      // 浅水淡出尺度(1/米)，越大淡出带越窄
        float foamHardnessIntensity = 1.0f; // FF _FoamHardnessIntensity
        float foamHardnessWidth = 0.02f;    // FF _FoamHardnessWidth
        float foamSoftVelocity = 0.5f;      // 软晕随流速加宽
        float foamSoftBase = 0.0f;          // 软晕基底宽度
        float foamSoftMax = 0.0f;           // 软晕上限
        float foamAlpha = 1.0f;             // FF _FoamColorBase.a：泡沫总不透明度
    };

    struct WaterMaterialGuiParams
    {
        // 水体颜色
        glm::vec4 shallowColor{0.38f, 0.62f, 0.72f, 1.0f};   // 浅水：浅青白
        glm::vec4 deepColor{0.02f, 0.12f, 0.28f, 1.0f};      // 深水：深青蓝
        glm::vec4 sedimentColor{0.42f, 0.30f, 0.16f, 1.0f};  // 泥沙颜色

        // 光学参数: x=F0基础反射率, y=反射强度, z=GGX粗糙度, w=泥沙混合量
        glm::vec4 opticalParams{0.02f, 0.35f, 0.06f, 1.5f};  // 默认接近水物理值

        // 太阳方向（与天空太阳一致）
        glm::vec3 sunDirection{-0.63f, 0.11f, 1.0f};
        float specularStrength = 6.0f;                       // 太阳高光强度（5~10）

        glm::vec4 fogParams{120.0f, 6000.0f, 0.4f, 0.0f};     // 雾效参数

        // 吸收系数（红、绿、蓝）
        glm::vec3 absorption{0.35f, 0.06f, 0.03f};           // 红光衰减最快

        float bedAlbedo = 0.85f;                             // 河床反照率
        float maxVisibleDepth = 8.0f;                        // 最大可见深度（米）

        float shallowBlend = 0.0f;                            // 吸收总倍率（越大越快变不透明）
        float depthUpwardBlend = 1.0f;                       // FF _WaterDepthUpwardBlend：俯视时等效光程加成
    
        // ===== FF 岸线两档（MF_WaterTransition）=====
        glm::vec3 absorptionShore{0.163f, 0.092f, 0.084f};   // 岸线档吸收(1/m)，比深水档更清澈
        glm::vec3 scatteringDeep{0.006f, 0.009f, 0.012f};    // 深水档散射(1/m)
        glm::vec3 scatteringShore{0.020f, 0.020f, 0.020f};   // 岸线档散射(1/m)，无色更强=乳白浅滩
        float scatterGain = 8.0f;                            // 散射增益（等效 HDR 入射光强）
        float foamScatterScale = 4.0f;                       // FF _FoamScatteringScale
        float shoreDepthNorm = 17.0f;                        // 深度归一化尺度(米)
        float shoreDistNorm = 200.0f;                        // 离岸距离归一化尺度(米)
        glm::vec3 colorBehind{0.63f, 0.30f, 0.05f};          // 水下背景色调
        float waterLevel = 2.0f;                             // 水面基准高度(米)

        // ===== FF 高光/粗糙度（MF_FluidWaterLayer）=====
        float specBias = 0.045f;          // Fresnel 偏置：正上方俯视时的基础反射
        float specScale = 1.0f;           // Fresnel 幅度
        float specPower = 4.0f;           // Fresnel 指数：越大反射越集中在掠射角
        float specHorizonFloor = 0.2f;    // 超出地平线距离后保留的高光底噪
        float specHorizonDistance = 9.0f; // 地平线基准距离(米)
        float specHorizonOffset = 6.0f;   // 相机高度对可见高光距离的放大系数
        float roughFromFresnel = 0.3f;    // 掠射角额外粗糙度
        float roughMin = 0.04f;           // 最小粗糙度（镜面锐度）
        float scatterDetails = 0.5f;      // Cheap Scattering 细节权重
        float scatterPower = 2.0f;        // Cheap Scattering 指数
        float scatterScale = 3.0f;        // Cheap Scattering 强度
        float normalFixStrength = 0.5f;   // 不可能法线修正强度（0=关闭，1=FF 原始强度）
    };

    struct QuadtreeGuiParams
    {
        glm::vec2 rootCenter{0.0f};           // 四叉树根节点中心
        float rootSize = 16384.0f;            // 根节点覆盖范围（米）
        int maxLevel = 8;                     // 最大细分层级
        int patchCellCount = 32;              // 每个 Tile 的网格单元数
        float fovYDegrees = 45.0f;            // 垂直视场角（度）
        float splitPixels = 9.0f;             // 分裂阈值（像素）
        float mergePixels = 6.0f;             // 合并阈值（像素）
        float minY = -40.0f;                  // 水面 AABB 最小 Y（米，包含 skirt）
        float maxY = 40.0f;                   // 水面 AABB 最大 Y（米，包含高潮头）

        int boreCoreLevel = 6;                // 潮头核心区域强制 LOD 层级
        int boreNearLevel = 5;                // 潮头附近过渡区域 LOD 层级
        float boreCoreWidth = 80.0f;          // 潮头核心范围（沿河距离，米）
        float boreNearWidth = 220.0f;         // 潮头附近过渡范围（沿河距离，米）

        int boreUltraLevel = 8;               // 潮头超精细区域 LOD 层级
        float boreUltraWidth = 24.0f;         // 潮头超精细范围（沿河距离，米）
    };

    struct CrestRibbonGuiParams
    {
        bool enabled = false;
        int lateralSegments = 256;          // 横向分段：越高越平滑，384 对 16km 江面够用
        int depthSegments = 4;              // 前后方向分段：6~8 即可
        float frontWidth = 6.0f;            // 潮头前方覆盖宽度(米)
        float wakeWidth = 18.0f;            // 潮头后方白水带宽度(米)
        float heightOffset = 0.22f;         // 整体抬离水面，避免 z-fighting
        float crestHeightOffset = 0.55f;    // 主脊额外高度
        float alpha = 0.92f;                // 主潮脊不透明度
        float edgeFade = 0.12f;             // 两岸淡出宽度（归一化横向）

        float curveMeters = 90.0f;           // 主潮线基础弯曲(米)
        float irregularCurveMeters = 140.0f; // 主潮线不规则弯曲(米)
        float curveFrequency = 3.0f;         // 横向弯曲频率
        float heightVariation = 0.75f;       // 潮脊高度低频起伏(米)

        float edgeJitterMeters = 18.0f;      // 片元级前沿抖动(米)
        float wakePatchThreshold = 0.25f;    // 白水团阈值，越高越碎
        float wakeFoamStrength = 2.0f;       // 白水团强度
        float wakeHoleStrength = 0.56f;      // 孔洞强度

        float hardCrestWidth = 6.0f;         // 主白线半宽(米)
        float wakeStart = 0.0f;              // 从潮头后方多少米开始出现大片浮沫
        float wakeEnd = 380.0f;              // 浮沫区结束距离(米)
        float wakeFeather = 80.0f;           // 浮沫区淡入/淡出宽度(米)
    };

    struct BoreWakeGuiParams
    {
        bool enabled = true;
        int resolution = 512;

        float wakeStart = 0.0f;
        float wakeEnd = 400.0f;
        float wakeFeather = 50.0f;
        float advectionSpeed = 80.0f;

        float sourceStrength = 2.4f;
        float aerationStrength = 0.0f;
        float foamStrength = 2.50f;
        float sedimentStrength = 0.0f;
        float turbulenceStrength = 0.0f;

        float aerationDecay = 0.75f;
        float foamDecay = 0.5f;
        float sedimentDecay = 0.08f;
        float turbulenceDecay = 0.55f;

        float patchThreshold = 0.56f;
        float warpStrength = 4.0f;
        float lateralFrequency = 8.0f;
        float backFrequency = 16.0f;
    };

    struct CrestRibbonVertex
    {
        glm::vec4 positionAlpha; // xyz=world position, w=alpha
        glm::vec4 param;   // x=lateral y=depth01 z=signedDistance w=randomSeed
        glm::vec4 param2;  // x=edgeJitter y=wakePatchThreshold z=wakeFoamStrength w=wakeHoleStrength
        glm::vec4 param3;  // x=time y=wakeWidth z=frontWidth w=reserved
        glm::vec4 param4;  // x=hardCrestWidth y=wakeStart z=wakeEnd w=wakeFeather

        static VkVertexInputBindingDescription GetBindingDescription();
        static std::array<VkVertexInputAttributeDescription, 5> GetAttributeDescriptions();
    };

    struct MultiBoreGuiParams
    {
        bool enabled = true;
        int seed = 1337;
        float minSpawnInterval = 20.0f;
        float maxSpawnInterval = 60.0f;
        float retryMinInterval = 0.5f;
        float retryMaxInterval = 1.0f;
        float baseSpeed = 64.0f;
        float removeMargin = 120.0f;
        float minimumSeparationPadding = 10.0f;
        float lateralExtent = 0.85f;   // 潮头横向覆盖到河宽的百分比
        float lateralFade = 0.18f;     // 两岸淡出宽度
    };

    enum class BoreFieldMode
    {
        SDFFlowMap = 0,     // 旧：Flow + Coordinate 两张图
        ProgressField = 1   // 新：单张 Progress 图
    };

    struct CrestNoiseGuiParams
    {
        float lateralFrequency = 2.4f;   // 横向(河宽)团块数
        float alongFrequencyX = 0.0f;    // 沿河频率(x轴)
        float alongFrequencyY = 0.0f;    // 沿河频率(y轴)
        float animationSpeed = 0.0f;     // 随潮头推进的流动速度
        float detailFrequency = 4.0f;    // 细碎波动频率
        float detailWeight = 0.0f;       // 大/细占比 [0,1]
        float amplitudeMin = 0.82f;      // 振幅下限
        float amplitudeMax = 1.18f;      // 振幅上限
        float wobbleStrength = 0.0f;     // 浪脊顶抖强度
        float wobbleFrequency = 0.0f;    // 浪脊顶抖频率
    };

private:
    void CreateDescriptorSetLayout();          // 创建几何描述符集布局（set=0），定义 FFT/Bore/UBO 等资源绑定
    void CreatePipelines();                    // 创建图形管线（实体+线框），绑定两个描述符集布局（set0/set1）
    void CreateWaterGrid();                    // 创建静态水面网格（128×128顶点），上传到设备本地内存
    void CreateWaterPatch();

    void CreateRiverResources();
    void CreateTerrainResources();

    // 依据当前 m_ShoreParams 重新烘焙岸线场并刷新 binding 21
    void RebakeShoreField();
    void RebuildBoreProfileResources();

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

    // Bore Wake
    void CreateBoreWakeResources();
    void CreateBoreWakeDescriptorSetLayouts();
    void CreateBoreWakePipelines();
    void CreateBoreWakeDescriptorPool();
    void CreateBoreWakeDescriptorSets();
    void InitializeBoreWakeImages();
    void UpdateBoreWakeParamsUniformBuffer(uint32_t frameIndex);
    void RecordBoreWakeSimulation(VkCommandBuffer commandBuffer, uint32_t frameIndex);

    void CreateFoamStateResources();
    void CreateFoamComputeDescriptorSetLayouts();
    void CreateFoamComputePipelines();
    void CreateFoamComputeDescriptorPool();
    void CreateFoamComputeDescriptorSets();
    void InitializeFoamStateImages();
    void RecordFoamSimulation(VkCommandBuffer commandBuffer, uint32_t frameIndex);
    
    water::RiverSamplePoint SampleRiverAtProgress(float progressMeters) const;
    void CreateCrestRibbonResources();
    void UpdateCrestRibbonBuffer(uint32_t frameIndex);
    void DrawCrestRibbon(VkCommandBuffer commandBuffer, uint32_t currentFrame);
    
    void UpdateCamera(float deltaTime);        // 处理键盘输入（WASD/Space/LCtrl）移动相机
    void UpdateCameraUniformBuffer(uint32_t frameIndex); // 写入当前帧的 CameraUBO（MVP 矩阵 + 调试模式）
    void UpdateWaterParamsUniformBuffer(uint32_t frameIndex); // 写入当前帧的 WaterParamsUBO（patch 长度、振幅、时间）
    void UpdateBoreFrontUniformBuffer(uint32_t frameIndex); // 写入当前帧的 BoreFrontUBO（波前位置、方向、时间）
    void UpdateBoreProfileUniformBuffer(uint32_t frameIndex); // 写入当前帧的 BoreProfileUBO（剖面尺寸、动画时间）
    void UpdateMultiBoreBuffers(uint32_t frameIndex);
    void ResetMultiBoreEvents();
    water::BoreEventManagerConfig BuildBoreEventManagerConfig() const;
    float ComputeProfilePhaseForProgress(float progressMeters, float phaseOffset) const;
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
    bool m_CameraControlEnabled = false;

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
    MultiBoreGuiParams m_MultiBoreGui{};

    BoreFieldMode m_BoreFieldMode = BoreFieldMode::ProgressField;
    CrestNoiseGuiParams m_CrestNoiseGui{};

    // 岸线场烘焙参数（GUI 可调，改后点 Rebake 重烘焙）
    water::ShoreFieldParams m_ShoreParams{};
    // GUI 请求重烘焙的延迟标志，在帧录制前的安全点执行
    bool m_ShoreRebakePending = false;
    bool m_BoreProfileRebuildPending = false;
    // 记录烘焙时使用的场配置，供 RebakeShoreField 复用
    water::RiverFieldConfig m_RiverFieldConfig{};

    // 配置参数：决定 FFT 网格大小和物理范围，供创建 WSTessendorfCPU 和分配纹理使用。
    water::Stage6OceanConfig m_OceanConfig =
        water::MakeStage6ReferenceOceanConfig();

    uint32_t m_FFTResolution =
        m_OceanConfig.spectrum.resolution;

    float m_LastSimulationDeltaTime = 0.0f;
    float m_BoreTime = 0.0f;
    float m_LastBoreDeltaTime = 0.0f;
    float m_BoreAccumulator = 0.0f;

    bool m_BorePaused = false;
    bool m_BoreEnabled = true;
    bool m_BoreUseLUT = false;
    bool m_BoreDebugRidgeEnabled = false;

    float m_MaxBorePassedProgress = -1.0e9f;   // 历史最远潮头推进距离(米)，单调不减

    water::BoreFrontParams m_BoreFrontParams{};
    water::FrontLUTData m_FrontLUT{};

    // Wave Profile
    water::BoreWaveProfileConfig m_BoreProfileConfig{};
    water::BoreWaveProfileData m_BoreProfileData{};

    float m_ProfileTime = 0.0f;
    float m_EventRepeatDuration = 30.0f;

    water::BoreProfileAnimationMode m_ProfileMode =
        water::BoreProfileAnimationMode::OneShot;

    bool m_ProfilePaused = false;
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
    std::vector<std::unique_ptr<vkp::Buffer>> m_MultiBoreUniformBuffers;
    std::vector<std::unique_ptr<vkp::Buffer>> m_BoreEventBuffers;
    std::array<water::BoreEventGPU, water::kMaxBoreEvents> m_BoreEventGpuScratch{};
    water::BoreEventManager m_BoreEventManager;
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
    std::unique_ptr<water::StaticDataTexture2D> m_ProgressFieldTexture;
    std::unique_ptr<water::StaticDataTexture2D> m_ShoreMaskTexture;
    water::FoamDetailTextureData m_FoamDetailData{};
    std::unique_ptr<water::StaticDataTexture2D> m_FoamDetailTexture;

    // Foam
    uint32_t m_FoamResolution = 512;
    uint32_t m_CurrentFoamStateIndex = 0;
    float m_LastFoamDeltaTime = 0.0f;

    std::array<std::unique_ptr<water::ComputeImage2D>, 2> m_FoamStateImages;
    std::unique_ptr<water::ComputeImage2D> m_FoamSourceVelocityImage;

    CrestRibbonGuiParams m_CrestRibbonGui{};
    BoreWakeGuiParams m_BoreWakeGui{};

    std::vector<std::unique_ptr<vkp::Buffer>> m_CrestRibbonVertexBuffers;
    uint32_t m_CrestRibbonVertexCapacity = 0;
    uint32_t m_CrestRibbonVertexCount = 0;

    std::unique_ptr<vkp::Pipeline> m_CrestRibbonPipeline;

    // Foam Simulation
    std::vector<std::unique_ptr<vkp::Buffer>> m_FoamSimulationUniformBuffers;

    std::unique_ptr<vkp::DescriptorSetLayout> m_FoamSourceSetLayout;
    std::unique_ptr<vkp::DescriptorSetLayout> m_FoamAdvectSetLayout;
    std::unique_ptr<vkp::DescriptorPool> m_FoamComputeDescriptorPool;

    std::unique_ptr<water::ComputePipeline> m_FoamSourcePipeline;
    std::unique_ptr<water::ComputePipeline> m_FoamAdvectPipeline;

    std::vector<VkDescriptorSet> m_FoamSourceSets;
    std::vector<std::array<VkDescriptorSet, 2>> m_FoamAdvectSets;

    // Bore Wake
    std::array<std::unique_ptr<water::ComputeImage2D>, 2> m_BoreWakeStateImages;
    std::unique_ptr<water::ComputeImage2D> m_BoreWakeSourceImage;
    uint32_t m_CurrentBoreWakeStateIndex = 0;

    std::vector<std::unique_ptr<vkp::Buffer>> m_BoreWakeParamsBuffers;

    std::unique_ptr<vkp::DescriptorSetLayout> m_BoreWakeSourceSetLayout;
    std::unique_ptr<vkp::DescriptorSetLayout> m_BoreWakeAdvectSetLayout;
    std::unique_ptr<vkp::DescriptorPool> m_BoreWakeDescriptorPool;

    std::unique_ptr<water::ComputePipeline> m_BoreWakeSourcePipeline;
    std::unique_ptr<water::ComputePipeline> m_BoreWakeAdvectPipeline;

    std::vector<VkDescriptorSet> m_BoreWakeSourceSets;
    std::vector<std::array<VkDescriptorSet, 2>> m_BoreWakeAdvectSets;

    // River Tile
    water::RiverSpline m_RiverSpline;
    std::unique_ptr<water::RiverField> m_RiverField;
    float m_RiverLength = 0.0f;
    float m_RiverBoreCurvatureMeters = 28.0f;

    water::Heightmap m_TerrainHeightmap;
    std::unique_ptr<water::WaterGrid> m_TerrainGrid;
    std::unique_ptr<vkp::Pipeline> m_TerrainPipeline;

    std::unique_ptr<vkp::Pipeline> m_SkyPipeline;
};