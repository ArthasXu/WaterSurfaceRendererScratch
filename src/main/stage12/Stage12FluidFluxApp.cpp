#include "main/stage12/Stage12FluidFluxApp.h"

#include "core/Log.h"
#include "gui/Gui.h"

#include "scene/water/render/WaterVertex.h"
#include "scene/water/bore/BoreWaveProfile.h"
#include "scene/water/foam/FoamDetailGenerator.h"
#include "scene/water/river/ProgressFieldBaker.h"
#include "scene/water/river/ShoreFieldBaker.h"
#include "scene/water/river/RiverFieldBundle.h"
#include "scene/water/bore/BoreWakeResourceContract.h"

#include <GLFW/glfw3.h>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/packing.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <array>
#include <cmath>
#include <sstream>
#include <stdexcept>
#include <iostream>
#include <algorithm>
#include <limits>
#include <imgui.h>

// Start()
// ├── VKP_INFO("Stage10RiverWaterApp started")
// ├── static_assert(sizeof(glm::vec4) == 16)
// ├── m_Camera.AddYawPitch(0.0f, -300.0f)               // 初始俯仰角
// ├── CreateDescriptorSetLayout()                       // 图形管线描述符布局：2 UBO + 6 纹理（3层位移+法线辅助）
// ├── CreatePipelines()                                 // 实体 & 线框图形管线（共用布局）
// ├── CreateWaterGrid()                                 // 静态 DEVICE_LOCAL 水面网格
// ├── CreateSamplers()                                  // 位移图采样器（NEAREST / LINEAR）
// ├── CreateGPUFFTSource()                              // 初始化 WSTessendorfGPU：
// │   ├── 创建 3 层 GPUFFT2D，每层独立静态资源（h0, h0MinusConj, waveData）和帧资源（ping-pong buffer, 图像）
// │   ├── 创建 Compute Pipeline（频谱初始化、频谱演化、Stockham IFFT、输出到纹理）
// │   ├── 为每层每帧创建描述符集（spectrum/ping-pong/output）
// │   └── 构建 GPUResources（每帧每层的 patch/振幅/图像信息）
// ├── CreateUniformBuffers()                            // CameraUBO + WaterParamsUBO（每飞行帧一个，HOST_VISIBLE）
// ├── CreateDescriptorPool()                            // 池子大小：UBO × 2N + COMBINED_IMAGE_SAMPLER × 6N
// └── CreateDescriptorSets()                            // 为每个飞行帧绑定：
//     ├── binding 0: CameraUBO
//     ├── binding 1: WaterParamsUBO
//     ├── binding 2‑3: Cascade 0 位移 & 法线辅助
//     ├── binding 4‑5: Cascade 1 位移 & 法线辅助
//     └── binding 6‑7: Cascade 2 位移 & 法线辅助

// 每帧运行流程（Application 主循环）：
// Loop()
// └── for each frame
//     ├── Update(timestep)                              // 更新相机、累计模拟时间
//     ├── PrepareFrame(frameIndex, imageIndex)           // 更新 Camera UBO 和 WaterParams UBO
//     └── Render(commandBuffer, imageIndex)              // 录制命令：
//         ├── m_TessendorfGPU->UpdateGPU(commandBuffer, currentFrame, dt)
//         │   └── 为每层 Cascade 依次：
//         │       ├── 图像屏障：GRAPHICS_READ → COMPUTE_WRITE
//         │       ├── 频谱更新 Compute Shader（h0 × e^{iωt} 写入 ping）
//         │       ├── 屏障：COMPUTE_WRITE → COMPUTE_READ
//         │       ├── Stockham 行/列 IFFT（ping-pong 交替）
//         │       ├── 输出 Compute Shader（打包缓冲 → displacementImage, normalAuxImage）
//         │       └── 图像屏障：COMPUTE_WRITE → GRAPHICS_READ
//         ├── BeginRenderPass
//         ├── 绑定图形管线（实体/线框）
//         ├── 绑定描述符集（currentFrame 的 UBO + 所有 Cascade 纹理）
//         ├── 绘制静态水面网格
//         └── EndRenderPass

// GPU 每帧数据流（完全在显存内）：
//   GPU 静态资源（h0, h0MinusConj, waveData）已存在
//   → 频谱演化 CS：根据时间 t 计算当前频域数据 → ping 缓冲区
//   → Stockham IFFT CS：频域 → 空间域（ping-pong）
//   → 输出 CS：打包缓冲 → displacementImage / normalAuxImage（GENERAL 布局）
//   → 屏障转换到 SHADER_READ_ONLY_OPTIMAL
//   → 顶点着色器采样位移纹理，偏移顶点
//   → 片段着色器输出最终颜色
// CPU 仅每帧传递时间参数和少量 push constants，无大数据传输。

namespace
{
struct PackedHalf4
{
    uint32_t rg = 0;
    uint32_t ba = 0;
};

static_assert(sizeof(PackedHalf4) == 8);

const char* DebugModeName(int mode)
{
    switch(mode){
    case 0: return "0 Final 最终效果";
    case 1: return "1 Height 高度场灰度图";
    case 2: return "2 Horizontal Displacement 水平位移可视化";
    case 3: return "3 Slope 斜率可视化";
    case 4: return "4 Breaking 波浪破碎判据";
    case 5: return "5 World Normal 世界空间法线";
    case 6: return "6 Bore Signed Distance 波前距离场（单浪）";
    case 7: return "7 Front Fade 波前长度淡入淡出掩码";
    case 8: return "8 Front U 波前线坐标";
    case 9: return "9 Front Normal 局部波前法线方向";
    case 10: return "10 Amplitude 振幅乘数";
    case 11: return "11 Foam Multiplier 泡沫乘数";
    case 12: return "12 Phase Offset 轮廓相位偏移";
    case 13: return "13 Profile U Wave Profile 距离轴坐标";
    case 14: return "14 Phase Offset (alt) 动画相位偏移（备用）";
    case 15: return "15 Forward Displacement 前向水平位移";
    case 16: return "16 Upward Displacement 向上垂直位移";
    case 17: return "17 Profile Foam Source Wave Profile 泡沫源";
    case 18: return "18 Crest Mask 浪尖掩码";
    case 19: return "19 dUpward/ds 向上位移的导数";
    case 20: return "20 Flow Speed 流速";
    case 21: return "21 Breaking Weight 破碎权重";
    case 22: return "22 FFT Suppression FFT 抑制权重";
    case 23: return "23 Total Slope 最终合成坡度";
    case 24: return "24 Final Normal 最终世界空间法线";
    case 25: return "25 Back Mask 潮后掩码";
    case 26: return "26 Final Displacement 最终位移";
    case 27: return "27 Profile Foam (source) Profile 泡沫源（单独）";
    case 28: return "28 FFT Jacobian Foam FFT Jacobian 泡沫";
    case 29: return "29 Slope Foam 坡度泡沫";
    case 30: return "30 Bore Breaking Foam Bore 破碎泡沫";
    case 31: return "31 Combined Foam Source 合并泡沫源";
    case 32: return "32 Foam Detail Phase0 三相位泡沫细节 Phase 0";
    case 33: return "33 Foam Detail Phase1 三相位泡沫细节 Phase 1";
    case 34: return "34 Foam Detail Phase2 三相位泡沫细节 Phase 2";
    case 35: return "35 Phase Weights 三相位混合权重";
    case 36: return "36 Foam Coverage 泡沫覆盖率";
    case 37: return "37 Foam Velocity 泡沫流速";
    case 38: return "38 State Foam 状态型泡沫";
    case 39: return "39 Final Foam 最终泡沫混合结果";
    case 40: return "40 River Flow 河流流向";
    case 41: return "41 River Progress 沿河归一化进度";
    case 42: return "42 River Lateral 横向归一化坐标";
    case 43: return "43 River Mask 水域掩码";
    case 44: return "44 Bank Distance 到岸的有符号距离";
    case 45: return "45 Wetness Base 河内=1 岸上线性淡出";
    case 46: return "46 Sand 岸线一条亮带";
    case 47: return "47 Terrain Height 岸上按坡度抬升";
    default: return "Custom";
    }
}

PackedHalf4 PackHalf4(glm::vec4 value)
{
    PackedHalf4 packed{};
    packed.rg =
        glm::packHalf2x16(
            glm::vec2(value.x, value.y)
        );

    packed.ba =
        glm::packHalf2x16(
            glm::vec2(value.z, value.w)
        );

    return packed;
}

std::vector<PackedHalf4> PackHalf4Vector(
    const std::vector<glm::vec4>& values
)
{
    std::vector<PackedHalf4> packed;
    packed.reserve(values.size());

    for(const glm::vec4& value : values){
        packed.push_back(PackHalf4(value));
    }

    return packed;
}
}

void Stage12FluidFluxApp::Start()
{
    VKP_INFO("Stage12FluidFluxApp started");

    static_assert(sizeof(glm::vec4) == 16);

    m_Camera.SetPosition(glm::vec3(940.0f, 255.0f, -475.0f));
    m_Camera.LookAt(glm::vec3(165.0f, 53.0f, -16.0f));

    CreateDescriptorSetLayout();
    CreateAppearanceDescriptorSetLayout();
    CreatePipelines();
    CreateWaterPatch();
    CreateSamplers();
    
    CreateRiverResources();
    CreateBoreFrontResources();
    CreateBoreProfileResources();
    
    CreateFoamResources();
    CreateFoamStateResources();
    CreateBoreWakeResources();
    CreateFoamComputeDescriptorSetLayouts();
    CreateBoreWakeDescriptorSetLayouts();
    CreateFoamComputePipelines();
    CreateBoreWakePipelines();
    CreateGPUFFTSource();

    CreateUniformBuffers();
    CreateTileInstanceBuffers();
    CreateCrestRibbonResources();

    CreateDescriptorPool();
    CreateAppearanceDescriptorPool();
    CreateFoamComputeDescriptorPool();
    CreateBoreWakeDescriptorPool();

    CreateDescriptorSets();
    CreateAppearanceDescriptorSets();
    CreateFoamComputeDescriptorSets();
    CreateBoreWakeDescriptorSets();

    CreateTerrainResources();

    InitializeFoamStateImages();
    InitializeBoreWakeImages();
    ResetMultiBoreEvents();

    SetupGui();
}

void Stage12FluidFluxApp::ShutdownApp()
{
    if(m_GuiDescriptorPool != VK_NULL_HANDLE){
        gui::Shutdown();
        vkDestroyDescriptorPool(GetDevice(), m_GuiDescriptorPool, nullptr);
        m_GuiDescriptorPool = VK_NULL_HANDLE;
    }

    m_SolidPipeline.reset();
    m_WireframePipeline.reset();
    m_FoamSourcePipeline.reset();
    m_FoamAdvectPipeline.reset();
    m_BoreWakeSourcePipeline.reset();
    m_BoreWakeAdvectPipeline.reset();
    m_TerrainPipeline.reset(); 
    m_TerrainGrid.reset();
    m_SkyPipeline.reset();
    m_CrestRibbonPipeline.reset();
    m_CrestRibbonVertexBuffers.clear();

    m_DescriptorSets.clear();
    m_AppearanceDescriptorSets.clear();
    m_FoamSourceSets.clear();
    m_FoamAdvectSets.clear();
    m_BoreWakeSourceSets.clear();
    m_BoreWakeAdvectSets.clear();

    m_FoamSourceVelocityImage.reset();
    m_FoamStateImages[0].reset();
    m_FoamStateImages[1].reset();

    m_BoreWakeSourceImage.reset();
    m_BoreWakeStateImages[0].reset();
    m_BoreWakeStateImages[1].reset();

    m_FoamDetailTexture.reset();
    m_FrontDerivativeTexture.reset();
    m_FrontParameterTexture.reset();
    m_BoreProfileDerivativeTexture.reset();
    m_BoreProfileDisplacementTexture.reset();
    m_RiverCoordinateTexture.reset();
    m_RiverFlowTexture.reset();
    m_ProgressFieldTexture.reset();
    m_ShoreMaskTexture.reset();

    m_FoamComputeDescriptorPool.reset();
    m_BoreWakeDescriptorPool.reset();
    m_AppearanceDescriptorPool.reset();
    m_DescriptorPool.reset();

    m_FoamSourceSetLayout.reset();
    m_FoamAdvectSetLayout.reset();
    m_BoreWakeSourceSetLayout.reset();
    m_BoreWakeAdvectSetLayout.reset();
    m_AppearanceDescriptorSetLayout.reset();
    m_DescriptorSetLayout.reset();

    m_TessendorfGPU.reset();

    m_FoamDetailSampler.reset();
    m_FoamStateSampler.reset();
    m_FFTSampler.reset();
    m_FrontLUTSampler.reset();
    m_BoreProfileSampler.reset();
    m_RiverSampler.reset();

    m_FoamSimulationUniformBuffers.clear();
    m_BoreWakeParamsBuffers.clear();
    m_FoamParamsUniformBuffers.clear();
    m_WaterMaterialUniformBuffers.clear();
    m_TileInstanceBuffers.clear();
    m_WaterParamsUniformBuffers.clear();
    m_CameraUniformBuffers.clear();
    m_BoreFrontUniformBuffers.clear();
    m_BoreProfileUniformBuffers.clear();
    m_RiverFieldUniformBuffers.clear();
    m_MultiBoreUniformBuffers.clear();
    m_BoreEventBuffers.clear();

    m_WaterQuadtree.reset();
    m_WaterPatchMesh.reset();
    m_WaterGrid.reset();
    m_RiverField.reset();
}

VkVertexInputBindingDescription
Stage12FluidFluxApp::CrestRibbonVertex::GetBindingDescription()
{
    VkVertexInputBindingDescription binding{};
    binding.binding = 0;
    binding.stride = sizeof(CrestRibbonVertex);
    binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
    return binding;
}

std::array<VkVertexInputAttributeDescription, 5>
Stage12FluidFluxApp::CrestRibbonVertex::GetAttributeDescriptions()
{
    std::array<VkVertexInputAttributeDescription, 5> attributes{};

    attributes[0].binding = 0;
    attributes[0].location = 0;
    attributes[0].format = VK_FORMAT_R32G32B32A32_SFLOAT;
    attributes[0].offset = offsetof(CrestRibbonVertex, positionAlpha);

    attributes[1].binding = 0;
    attributes[1].location = 1;
    attributes[1].format = VK_FORMAT_R32G32B32A32_SFLOAT;
    attributes[1].offset = offsetof(CrestRibbonVertex, param);

    attributes[2].binding = 0;
    attributes[2].location = 2;
    attributes[2].format = VK_FORMAT_R32G32B32A32_SFLOAT;
    attributes[2].offset = offsetof(CrestRibbonVertex, param2);

    attributes[3].binding = 0;
    attributes[3].location = 3;
    attributes[3].format = VK_FORMAT_R32G32B32A32_SFLOAT;
    attributes[3].offset = offsetof(CrestRibbonVertex, param3);

    attributes[4].binding = 0;
    attributes[4].location = 4;
    attributes[4].format = VK_FORMAT_R32G32B32A32_SFLOAT;
    attributes[4].offset = offsetof(CrestRibbonVertex, param4);

    return attributes;
}

void Stage12FluidFluxApp::CreatePipelines()
{
    vkp::PipelineConfig config{};

    config.descriptorSetLayouts = {
        *m_DescriptorSetLayout,
        *m_AppearanceDescriptorSetLayout
    };

    auto bindingDescription =
        water::WaterPatchVertex::GetBindingDescription();

    auto attributeDescriptions =
        water::WaterPatchVertex::GetAttributeDescriptions();

    config.bindingDescriptions = {bindingDescription};
    config.attributeDescriptions = {
        attributeDescriptions[0],
        attributeDescriptions[1]
    };

    config.depthTestEnable = true;
    config.depthWriteEnable = true;    // 水面是单值高度场，每像素只该有一个可见片元，
                                       // 必须写深度。关闭时水体片元之间没有任何深度仲裁，
                                       // 后画的 tile 会无条件盖住先画的，表现为块状水面
                                       // 糊住潮头浪墙与白沫。
    config.depthCompareOp = VK_COMPARE_OP_LESS;
    config.blendEnable = true;         // 开 alpha 混合：浅水透出水下地形

    config.cullMode = VK_CULL_MODE_NONE;
    config.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    
    config.polygonMode = VK_POLYGON_MODE_FILL;

    m_SolidPipeline = std::make_unique<vkp::Pipeline>(
        GetDevice(),
        GetRenderPass(),
        "shaders/water/stage12_fluid_flux.vert.spv",
        "shaders/water/stage12_fluid_flux.frag.spv",
        config
    );

    if(GetDevice().SupportsFillModeNonSolid()){
        vkp::PipelineConfig wireframeConfig = config;
        wireframeConfig.polygonMode = VK_POLYGON_MODE_LINE;

        m_WireframePipeline = std::make_unique<vkp::Pipeline>(
            GetDevice(),
            GetRenderPass(),
            "shaders/water/stage12_fluid_flux.vert.spv",
            "shaders/water/stage12_fluid_flux.frag.spv",
            wireframeConfig
        );
    }

    // 泡沫源管线
    vkp::PipelineConfig crestRibbonConfig{};
    crestRibbonConfig.descriptorSetLayouts = {
        *m_DescriptorSetLayout,
        *m_AppearanceDescriptorSetLayout
    };

    auto crestBinding =
        CrestRibbonVertex::GetBindingDescription();

    auto crestAttributes =
        CrestRibbonVertex::GetAttributeDescriptions();

    crestRibbonConfig.bindingDescriptions = {crestBinding};
    crestRibbonConfig.attributeDescriptions = {
        crestAttributes[0],
        crestAttributes[1],
        crestAttributes[2],
        crestAttributes[3],
        crestAttributes[4]
    };

    // CrestRibbon 是表面泡沫 overlay，不参与真实水面深度仲裁。
    // 水面已经先写 depth；如果这里开 depth test，第二/第三潮头在已抬高水面下会被挡掉。
    crestRibbonConfig.depthTestEnable = false;
    crestRibbonConfig.depthWriteEnable = false;
    crestRibbonConfig.depthCompareOp = VK_COMPARE_OP_ALWAYS;
    crestRibbonConfig.blendEnable = true;
    crestRibbonConfig.cullMode = VK_CULL_MODE_NONE;
    crestRibbonConfig.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    crestRibbonConfig.polygonMode = VK_POLYGON_MODE_FILL;

    m_CrestRibbonPipeline =
        std::make_unique<vkp::Pipeline>(
            GetDevice(),
            GetRenderPass(),
            "shaders/water/bore_crest.vert.spv",
            "shaders/water/bore_crest.frag.spv",
            crestRibbonConfig
        );

    // 水面地形管线
    vkp::PipelineConfig terrainConfig{};
    terrainConfig.descriptorSetLayouts = {
        *m_DescriptorSetLayout,
        *m_AppearanceDescriptorSetLayout
    };
    auto terrainBinding = water::WaterVertex::GetBindingDescription();
    auto terrainAttrs = water::WaterVertex::GetAttributeDescriptions();
    terrainConfig.bindingDescriptions = { terrainBinding };
    terrainConfig.attributeDescriptions = {
        terrainAttrs[0], terrainAttrs[1], terrainAttrs[2] };
    terrainConfig.depthTestEnable = true;
    terrainConfig.depthWriteEnable = true;
    terrainConfig.depthCompareOp = VK_COMPARE_OP_LESS;
    terrainConfig.cullMode = VK_CULL_MODE_NONE;
    terrainConfig.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    terrainConfig.polygonMode = VK_POLYGON_MODE_FILL;

    m_TerrainPipeline = std::make_unique<vkp::Pipeline>(
        GetDevice(), GetRenderPass(),
        "shaders/water/terrain.vert.spv",
        "shaders/water/terrain.frag.spv",
        terrainConfig);

    // 天空盒管线
    vkp::PipelineConfig skyConfig{};
    skyConfig.descriptorSetLayouts = {
        *m_DescriptorSetLayout,
        *m_AppearanceDescriptorSetLayout
    };
    // 无顶点缓冲：bindingDescriptions/attributeDescriptions 保持空
    skyConfig.depthTestEnable  = false;
    skyConfig.depthWriteEnable = false;
    skyConfig.cullMode  = VK_CULL_MODE_NONE;
    skyConfig.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    skyConfig.polygonMode = VK_POLYGON_MODE_FILL;

    m_SkyPipeline = std::make_unique<vkp::Pipeline>(
        GetDevice(), GetRenderPass(),
        "shaders/water/sky.vert.spv",
        "shaders/water/sky.frag.spv",
        skyConfig);
}

void Stage12FluidFluxApp::CreateFoamComputePipelines()
{
    water::ComputePipelineConfig sourceConfig{};
    sourceConfig.descriptorSetLayouts = {
        *m_FoamSourceSetLayout
    };

    m_FoamSourcePipeline =
        std::make_unique<water::ComputePipeline>(
            GetDevice(),
            "shaders/water/foam/foam_source.comp.spv",
            sourceConfig
        );

    water::ComputePipelineConfig advectConfig{};
    advectConfig.descriptorSetLayouts = {
        *m_FoamAdvectSetLayout
    };

    m_FoamAdvectPipeline =
        std::make_unique<water::ComputePipeline>(
            GetDevice(),
            "shaders/water/foam/foam_advect.comp.spv",
            advectConfig
        );
}

void Stage12FluidFluxApp::CreateBoreWakePipelines()
{
    water::ComputePipelineConfig sourceConfig{};
    sourceConfig.descriptorSetLayouts = {*m_BoreWakeSourceSetLayout};

    m_BoreWakeSourcePipeline =
        std::make_unique<water::ComputePipeline>(
            GetDevice(),
            "shaders/water/bore/bore_wake_source.comp.spv",
            sourceConfig
        );

    water::ComputePipelineConfig advectConfig{};
    advectConfig.descriptorSetLayouts = {*m_BoreWakeAdvectSetLayout};

    m_BoreWakeAdvectPipeline =
        std::make_unique<water::ComputePipeline>(
            GetDevice(),
            "shaders/water/bore/bore_wake_advect.comp.spv",
            advectConfig
        );
}

// 它定义了一套接口规范：
    // 这个描述符集有多少个 binding。
    // 每个 binding 的类型是什么（UBO、纹理、存储缓冲等）。
    // 哪些着色器阶段可以访问（顶点、片段、计算）。

// ===== 几何描述符集布局（set = 0）：水面几何与物理资源 =====
// 包含相机、FFT 波浪、涌潮波前（BoreFront）和涌潮剖面（BoreProfile）的全部资源
void Stage12FluidFluxApp::CreateDescriptorSetLayout()
{
    m_DescriptorSetLayout = vkp::DescriptorSetLayout::Builder(GetDevice())
        // Binding 0：CameraUBO（MVP 矩阵 + 调试模式），顶点/片段着色器可访问
        .AddBinding(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT)
        // Binding 1：WaterParamsUBO（三层 FFT 的 patch 长度、振幅缩放、时间、choppy/法线强度），顶点/片段着色器可访问
        .AddBinding(1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT)
        // Binding 2：fftDisplacement0（短波/高频位移纹理），顶点/片段着色器可访问
        .AddBinding(2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT)
        // Binding 3：fftNormalAux0（短波/高频法线辅助纹理），顶点/片段着色器可访问
        .AddBinding(3, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT)
        // Binding 4：fftDisplacement1（中波/中频位移纹理），顶点/片段着色器可访问
        .AddBinding(4, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT)
        // Binding 5：fftNormalAux1（中波/中频法线辅助纹理），顶点/片段着色器可访问
        .AddBinding(5, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT)
        // Binding 6：fftDisplacement2（长波/低频位移纹理），顶点/片段着色器可访问
        .AddBinding(6, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT)
        // Binding 7：fftNormalAux2（长波/低频法线辅助纹理），顶点/片段着色器可访问
        .AddBinding(7, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT)
        // Binding 8：BoreFrontUBO（涌潮波前原点、速度、时间、方向、长度、淡出、LUT 信息），顶点/片段着色器可访问
        .AddBinding(8, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT)
        // Binding 9：frontParameterLUT（波前弯曲偏移、振幅乘数、泡沫乘数、相位偏移），顶点/片段着色器可访问
        .AddBinding(9, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT)
        // Binding 10：frontDerivativeLUT（波前偏移导数，用于局部法线修正），顶点/片段着色器可访问
        .AddBinding(10, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT)
        // Binding 11：BoreProfileUBO（剖面尺寸、动画时间、FFT 抑制系数、水位抬升参数等），顶点/片段着色器可访问
        .AddBinding(11, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT)
        // Binding 12：boreProfileDisplacement（Wave Profile 位移纹理，含前向/向上位移、泡沫源、浪尖掩码），顶点/片段着色器可访问
        .AddBinding(12, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT)
        // Binding 13：boreProfileDerivative（Wave Profile 导数纹理，含 dForward/ds、dUpward/ds、流速、破碎权重），顶点/片段着色器可访问
        .AddBinding(13, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT)
        // Binding 14：WaterTileGPU SSBO，每个 instance 读取一个 Tile 的世界变换和 LOD 元数据
        .AddBinding(14, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_VERTEX_BIT)
        // Binding 15：RiverFieldUBO，河道 domain、riverLength、单潮头进度、河口曲率
        .AddBinding(15, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT)
        // Binding 16：River Flow Map，RG=flow direction, B=amplitude, A=water mask
        .AddBinding(16, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT)
        // Binding 17：River Coordinate Map，R=progress, G=lateral, B=bank distance, A=curvature
        .AddBinding(17, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT)
        .AddBinding(18, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT)
        .AddBinding(19, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT)
        .AddBinding(20, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT)
        .AddBinding(21, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT)
        .Build();
}

// ===== 外观描述符集布局（set = 1）：泡沫与材质资源 =====
// 与几何/物理管线完全解耦，未来适配不同材质或 FFT 源时只需替换此 set
void Stage12FluidFluxApp::CreateAppearanceDescriptorSetLayout()
{
    m_AppearanceDescriptorSetLayout =
        vkp::DescriptorSetLayout::Builder(GetDevice())
            // Binding 0：FoamParamsUBO（动画时间、泡沫源权重、阈值、外观参数、状态参数），顶点/片段着色器可访问
            .AddBinding(
                0,
                VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                VK_SHADER_STAGE_VERTEX_BIT |
                    VK_SHADER_STAGE_FRAGMENT_BIT
            )
            // Binding 1：FoamDetailTexture（预生成的周期性泡沫细胞纹理，R=覆盖度，GB=法线扰动，A=破碎噪声），片段着色器可访问
            .AddBinding(
                1,
                VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                VK_SHADER_STAGE_FRAGMENT_BIT
            )
            .AddBinding(
                2,
                VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                VK_SHADER_STAGE_FRAGMENT_BIT
            )
            .AddBinding(
                3,
                VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                VK_SHADER_STAGE_FRAGMENT_BIT
            )
            .AddBinding(
                4,
                VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                VK_SHADER_STAGE_FRAGMENT_BIT
            )
            // Binding 5：BoreWakeState0（状态型白水/含气/泥沙/湍流 Ping）
            .AddBinding(
                5,
                VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                VK_SHADER_STAGE_FRAGMENT_BIT
            )
            // Binding 6：BoreWakeState1（状态型白水/含气/泥沙/湍流 Pong）
            .AddBinding(
                6,
                VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                VK_SHADER_STAGE_FRAGMENT_BIT
            )
            .Build();
}

// ===== 泡沫计算描述符集布局（set = 2） =====
void Stage12FluidFluxApp::CreateFoamComputeDescriptorSetLayouts()
{
    m_FoamSourceSetLayout =
        vkp::DescriptorSetLayout::Builder(GetDevice())
            .AddBinding(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT)
            .AddBinding(1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT)
            .AddBinding(2, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT)
            .AddBinding(3, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT)
            .AddBinding(4, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_COMPUTE_BIT)
            .AddBinding(5, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_COMPUTE_BIT)
            .AddBinding(6, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_COMPUTE_BIT)
            .AddBinding(7, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_COMPUTE_BIT)
            .AddBinding(8, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_COMPUTE_BIT)
            .AddBinding(9, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_COMPUTE_BIT)
            .AddBinding(10, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_COMPUTE_BIT)
            .AddBinding(11, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_COMPUTE_BIT)
            .AddBinding(12, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_COMPUTE_BIT)
            .AddBinding(13, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_COMPUTE_BIT)
            .AddBinding(14, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT)
            .AddBinding(15, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_SHADER_STAGE_COMPUTE_BIT)
            .AddBinding(16, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT)
            .AddBinding(17, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT)
            .AddBinding(18, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_COMPUTE_BIT)
            .AddBinding(19, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_COMPUTE_BIT)
            .AddBinding(20, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_COMPUTE_BIT)
            .Build();

    m_FoamAdvectSetLayout =
        vkp::DescriptorSetLayout::Builder(GetDevice())
            .AddBinding(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT)
            .AddBinding(1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_COMPUTE_BIT)
            .AddBinding(2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_COMPUTE_BIT)
            .AddBinding(3, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_SHADER_STAGE_COMPUTE_BIT)
            .AddBinding(4, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_COMPUTE_BIT)
            .AddBinding(5, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT)
            .Build();
}

void Stage12FluidFluxApp::CreateBoreWakeDescriptorSetLayouts()
{
    m_BoreWakeSourceSetLayout =
        vkp::DescriptorSetLayout::Builder(GetDevice())
            .AddBinding(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT)
            .AddBinding(1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT)
            .AddBinding(2, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT)
            .AddBinding(3, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT)
            .AddBinding(4, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_COMPUTE_BIT)
            .AddBinding(5, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_COMPUTE_BIT)
            .AddBinding(6, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_COMPUTE_BIT)
            .AddBinding(7, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_SHADER_STAGE_COMPUTE_BIT)
            .Build();

    m_BoreWakeAdvectSetLayout =
        vkp::DescriptorSetLayout::Builder(GetDevice())
            .AddBinding(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT)
            .AddBinding(1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_COMPUTE_BIT)
            .AddBinding(2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_COMPUTE_BIT)
            .AddBinding(3, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_SHADER_STAGE_COMPUTE_BIT)
            .AddBinding(4, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_COMPUTE_BIT)
            .Build();
}

void Stage12FluidFluxApp::CreateWaterGrid()
{
    water::WaterGridConfig config{};
    config.cellCountX = 128;
    config.cellCountZ = 128;
    config.sizeX = 256.0f;
    config.sizeZ = 256.0f;
    config.origin = glm::vec3(0.0f);

    m_WaterGrid = std::make_unique<water::WaterGrid>(
        GetPhysicalDevice(),
        GetDevice(),
        GetCommandPool(),
        GetDevice().GetGraphicsQueue(),
        config,
        water::WaterGridUploadMode::StaticDeviceLocal
    );
}

void Stage12FluidFluxApp::CreateWaterPatch()
{
    m_WaterPatchMesh =
        std::make_unique<water::WaterPatchMesh>(
            GetPhysicalDevice(),
            GetDevice(),
            GetCommandPool(),
            GetDevice().GetGraphicsQueue(),
            static_cast<uint32_t>(m_QuadtreeGui.patchCellCount)
        );

    water::WaterQuadtreeConfig config{};
    config.rootCenter = m_QuadtreeGui.rootCenter;
    config.rootSize = m_QuadtreeGui.rootSize;
    config.maxLevel = static_cast<uint32_t>(m_QuadtreeGui.maxLevel);
    config.patchCellCount = static_cast<uint32_t>(m_QuadtreeGui.patchCellCount);
    config.fovYRadians = glm::radians(m_QuadtreeGui.fovYDegrees);
    config.splitPixels = m_QuadtreeGui.splitPixels;
    config.mergePixels = m_QuadtreeGui.mergePixels;
    config.minY = m_QuadtreeGui.minY;
    config.maxY = m_QuadtreeGui.maxY;
    
    // 将分类/分级函数注入四叉树配置
    config.classifyTile =
        [this](water::WaterTile& tile)
        {
            return ClassifyRiverTile(tile);
        };

    config.requiredLevel =
        [this](const water::WaterTile& tile)
        {
            return GetRiverRequiredLevel(tile);
        };

    m_WaterQuadtree =
        std::make_unique<water::WaterQuadtree>(
            config
        );
}

// 生成 U 形 field 并上传 GPU
void Stage12FluidFluxApp::CreateRiverResources()
{
    // struct RiverControlPoint
    // {
    //     glm::vec2 position{0.0f};       // 控制点的世界坐标位置
    //     float halfWidth = 100.0f;       // 该点处的河道半宽（米）
    //     float boreAmplitude = 1.0f;     // 该点处的涌潮振幅缩放系数
    //     float curvatureWeight = 0.0f;   // 曲率权重，用于控制弯曲处波前形变
    // };
    // 开放河道；入口在 root 外，避免端帽闭合
    std::vector<water::RiverControlPoint> controlPoints =
    {
        // {世界XZ}, halfWidth(米), boreAmplitude, curvatureWeight
        {{ -200.0f,  7600.0f}, 6000.0f, 0.60f, 0.60f},  // 入海口：极宽
        {{ -400.0f,  6000.0f}, 4000.0f, 0.80f, 0.45f},
        {{-1400.0f,  2900.0f}, 3200.0f, 0.92f, 0.35f},
        {{-1500.0f,  1400.0f}, 2600.0f, 1.05f, 0.25f},
        {{-1200.0f,     0.0f}, 2200.0f, 1.15f, 0.15f},
        {{ -400.0f, -1600.0f}, 1800.0f, 1.20f, 0.10f},
        {{  700.0f, -3200.0f}, 1500.0f, 1.15f, 0.05f},
        {{ 2200.0f, -4800.0f}, 1300.0f, 1.05f, 0.00f},
        {{ 4000.0f, -6400.0f}, 1200.0f, 1.00f, 0.00f}
    };

    // 定义并构建河流曲线
    m_RiverSpline.Build(
        controlPoints,
        24
    );

    water::RiverFieldConfig fieldConfig{};
    fieldConfig.worldMin = glm::vec2(-8192.0f, -8192.0f);
    fieldConfig.worldSize = 16384.0f;
    fieldConfig.resolution = 4096;
    fieldConfig.bankFade = 4.0f;
    fieldConfig.bankFadeDistance = 16.0f;   

    // 保存场配置，供 RebakeShoreField 复用
    m_RiverFieldConfig = fieldConfig;
    m_TerrainHeightmap = water::LoadHeightmap("assets/terrain/heightmap.png");

    // ===== 优先读离线烘焙的 bundle，缺失/失效则回退到即时烘焙 =====
    // 昂贵的逐像素 spline.Project 烘焙已移到 river-field-baker 工具。
    // spline 已 Build（成本低），GUI 的 RebakeShoreField 仍可用。
    std::vector<glm::vec4> flowField;
    std::vector<glm::vec4> coordinateField;
    std::vector<glm::vec4> progressField;
    std::vector<glm::vec4> shoreField;

    water::RiverFieldBundle bundle{};
    if(water::LoadRiverFieldBundle("assets/river/river_field.bin", bundle)){
        VKP_INFO("Loaded river field bundle from disk (baking skipped)");
        fieldConfig = bundle.config;
        m_RiverFieldConfig = bundle.config;
        m_RiverLength = bundle.riverLength;
        flowField = std::move(bundle.flow);
        coordinateField = std::move(bundle.coordinate);
        progressField = std::move(bundle.progress);
        shoreField = std::move(bundle.shore);
    } else {
        VKP_INFO("River field bundle missing/invalid; baking at runtime (slow). Run river-field-baker to cache it.");
        water::RiverFieldData fieldData =
            water::BakeRiverField(fieldConfig, m_RiverSpline);
        m_RiverLength = fieldData.riverLength;
        flowField = std::move(fieldData.flow);
        coordinateField = std::move(fieldData.coordinate);
        progressField =
            std::move(water::BakeProgressField(fieldConfig, m_RiverSpline).field);
        shoreField =
            std::move(water::BakeShoreField(fieldConfig, m_RiverSpline, m_ShoreParams, &m_TerrainHeightmap).field);
    }

    // ===== 打包并上传为 GPU 纹理 =====
    std::vector<PackedHalf4> flowPacked = PackHalf4Vector(flowField);
    std::vector<PackedHalf4> shorePacked = PackHalf4Vector(shoreField);

    VkDeviceSize halfTextureSize =
        static_cast<VkDeviceSize>(fieldConfig.resolution) *
        static_cast<VkDeviceSize>(fieldConfig.resolution) *
        sizeof(PackedHalf4);

    VkDeviceSize coordinateTextureSize =
        static_cast<VkDeviceSize>(coordinateField.size()) *
        sizeof(glm::vec4);

    VkDeviceSize progressTextureSize =
        static_cast<VkDeviceSize>(progressField.size()) *
        sizeof(glm::vec4);

    m_RiverFlowTexture =
        std::make_unique<water::StaticDataTexture2D>(
            GetPhysicalDevice(),
            GetDevice(),
            GetCommandPool(),
            GetDevice().GetGraphicsQueue(),
            fieldConfig.resolution,
            fieldConfig.resolution,
            VK_FORMAT_R16G16B16A16_SFLOAT,
            flowPacked.data(),
            halfTextureSize
        );

    m_RiverCoordinateTexture =
        std::make_unique<water::StaticDataTexture2D>(
            GetPhysicalDevice(),
            GetDevice(),
            GetCommandPool(),
            GetDevice().GetGraphicsQueue(),
            fieldConfig.resolution,
            fieldConfig.resolution,
            VK_FORMAT_R32G32B32A32_SFLOAT,
            coordinateField.data(),
            coordinateTextureSize
        );

    m_ProgressFieldTexture =
        std::make_unique<water::StaticDataTexture2D>(
            GetPhysicalDevice(),
            GetDevice(),
            GetCommandPool(),
            GetDevice().GetGraphicsQueue(),
            fieldConfig.resolution,
            fieldConfig.resolution,
            VK_FORMAT_R32G32B32A32_SFLOAT,
            progressField.data(),
            progressTextureSize
        );

    m_ShoreMaskTexture =
        std::make_unique<water::StaticDataTexture2D>(
            GetPhysicalDevice(),
            GetDevice(),
            GetCommandPool(),
            GetDevice().GetGraphicsQueue(),
            fieldConfig.resolution,
            fieldConfig.resolution,
            VK_FORMAT_R16G16B16A16_SFLOAT,
            shorePacked.data(),
            halfTextureSize
        );

    // ===== 重建 CPU 端 RiverField（供四叉树 LOD 剔除查询）=====
    water::RiverFieldData riverFieldData{};
    riverFieldData.config = fieldConfig;
    riverFieldData.riverLength = m_RiverLength;
    riverFieldData.flow = std::move(flowField);
    riverFieldData.coordinate = std::move(coordinateField);

    m_RiverField =
        std::make_unique<water::RiverField>(
            std::move(riverFieldData)
        );
}

void Stage12FluidFluxApp::CreateTerrainResources()
{
    water::WaterGridConfig cfg{};
    cfg.cellCountX = 512;
    cfg.cellCountZ = 512;
    cfg.sizeX = m_RiverFieldConfig.worldSize;
    cfg.sizeZ = m_RiverFieldConfig.worldSize;
    // WaterGrid 顶点用 (u-0.5)*size 自居中于 origin，
    // 因此 origin 要取域中心 worldMin + worldSize/2，才能覆盖 [worldMin, worldMin+worldSize]
    cfg.origin = glm::vec3(
        m_RiverFieldConfig.worldMin.x + m_RiverFieldConfig.worldSize * 0.5f,
        0.0f,
        m_RiverFieldConfig.worldMin.y + m_RiverFieldConfig.worldSize * 0.5f);

    m_TerrainGrid = std::make_unique<water::WaterGrid>(
        GetPhysicalDevice(), GetDevice(), GetCommandPool(),
        GetDevice().GetGraphicsQueue(), cfg,
        water::WaterGridUploadMode::StaticDeviceLocal);
}

void Stage12FluidFluxApp::RebakeShoreField()
{
    // 等待 GPU 空闲，确保旧纹理不再被任何在飞命令引用
    vkDeviceWaitIdle(GetDevice());

    // 用当前 GUI 参数重新烘焙岸线场
    water::ShoreFieldData shoreData =
        water::BakeShoreField(m_RiverFieldConfig, m_RiverSpline, m_ShoreParams, &m_TerrainHeightmap);

    std::vector<PackedHalf4> shorePacked =
        PackHalf4Vector(shoreData.field);

    // 重建 GPU 纹理（StaticDataTexture2D 无原地更新接口）
    m_ShoreMaskTexture =
        std::make_unique<water::StaticDataTexture2D>(
            GetPhysicalDevice(),
            GetDevice(),
            GetCommandPool(),
            GetDevice().GetGraphicsQueue(),
            m_RiverFieldConfig.resolution,
            m_RiverFieldConfig.resolution,
            VK_FORMAT_R16G16B16A16_SFLOAT,
            shorePacked.data(),
            static_cast<VkDeviceSize>(shorePacked.size()) * sizeof(PackedHalf4)
        );

    // 新纹理 view 变化，原地覆盖各帧描述符集的 binding 21（不重新分配池）
    for(uint32_t i = 0; i < GetMaxFramesInFlight(); ++i){
        VkDescriptorImageInfo shoreMaskInfo =
            m_ShoreMaskTexture->GetDescriptorInfo(*m_RiverSampler);

        vkp::DescriptorWriter(*m_DescriptorSetLayout, *m_DescriptorPool)
            .WriteImage(21, &shoreMaskInfo)
            .Overwrite(m_DescriptorSets[i]);
    }
}

void Stage12FluidFluxApp::RebuildBoreProfileResources()
{
    vkDeviceWaitIdle(GetDevice());

    CreateBoreProfileResources();

    for(uint32_t i = 0; i < GetMaxFramesInFlight(); ++i){
        VkDescriptorImageInfo boreProfileDisplacementInfo =
            m_BoreProfileDisplacementTexture->GetDescriptorInfo(*m_BoreProfileSampler);

        VkDescriptorImageInfo boreProfileDerivativeInfo =
            m_BoreProfileDerivativeTexture->GetDescriptorInfo(*m_BoreProfileSampler);

        vkp::DescriptorWriter(*m_DescriptorSetLayout, *m_DescriptorPool)
            .WriteImage(12, &boreProfileDisplacementInfo)
            .WriteImage(13, &boreProfileDerivativeInfo)
            .Overwrite(m_DescriptorSets[i]);

        vkp::DescriptorWriter(*m_FoamSourceSetLayout, *m_FoamComputeDescriptorPool)
            .WriteImage(6, &boreProfileDisplacementInfo)
            .WriteImage(7, &boreProfileDerivativeInfo)
            .Overwrite(m_FoamSourceSets[i]);
    }

    m_ProfileTime = m_BoreProfileConfig.duration * m_BoreProfileGui.fixedPhase;
}

void Stage12FluidFluxApp::CreateSamplers()
{
    VkFormatFeatureFlags linearFeatures =
        VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT |
        VK_FORMAT_FEATURE_TRANSFER_DST_BIT |
        VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT;

    VkFilter filter = VK_FILTER_NEAREST;

    if(GetDevice().SupportsFormatFeatures(
        VK_FORMAT_R32G32B32A32_SFLOAT,
        VK_IMAGE_TILING_OPTIMAL,
        linearFeatures
    )){
        filter = VK_FILTER_LINEAR;
    }

    m_FFTSampler = std::make_unique<water::WaterSampler>(
        GetDevice(),
        filter
    );

    m_FrontLUTSampler = std::make_unique<water::WaterSampler>(
        GetDevice(),
        VK_FILTER_LINEAR,
        VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE
    );

    m_BoreProfileSampler = std::make_unique<water::WaterSampler>(
        GetDevice(),
        VK_FILTER_LINEAR,
        VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE
    );

    m_RiverSampler = std::make_unique<water::WaterSampler>(
        GetDevice(),
        VK_FILTER_LINEAR,
        VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE
    );

    m_FoamDetailSampler = std::make_unique<water::WaterSampler>(
        GetDevice(),
        VK_FILTER_LINEAR,
        VK_SAMPLER_ADDRESS_MODE_REPEAT,
        8.0f
    );

    m_FoamStateSampler = std::make_unique<water::WaterSampler>(
        GetDevice(),
        VK_FILTER_LINEAR,
        VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE
    );
}

void Stage12FluidFluxApp::CreateBoreFrontResources()
{
    m_BoreFrontParams.origin = glm::vec2(0.0f);
    m_BoreFrontParams.direction = glm::normalize(glm::vec2(1.0f, 0.15f));
    m_BoreFrontParams.speed = 64.0f;
    m_BoreFrontParams.frontLength = 1000.0f;
    m_BoreFrontParams.initialOffset = 300.0f;
    m_BoreFrontParams.edgeFadeFraction = 0.03f;

    m_FrontLUT =
        water::GenerateDeterministicFrontLUT(1024);

    m_FrontParameterTexture =
        std::make_unique<water::StaticFloatTexture2D>(
            GetPhysicalDevice(),
            GetDevice(),
            GetCommandPool(),
            GetDevice().GetGraphicsQueue(),
            m_FrontLUT.resolution,
            1,
            m_FrontLUT.parameters
        );

    m_FrontDerivativeTexture =
        std::make_unique<water::StaticFloatTexture2D>(
            GetPhysicalDevice(),
            GetDevice(),
            GetCommandPool(),
            GetDevice().GetGraphicsQueue(),
            m_FrontLUT.resolution,
            1,
            m_FrontLUT.derivatives
        );
}

void Stage12FluidFluxApp::CreateBoreProfileResources()
{
    // 保留 GUI 中已修改的 m_BoreProfileConfig；不要在重建纹理时恢复默认值。
    m_BoreProfileData =
        water::GenerateAnimatedBoreWaveProfile(m_BoreProfileConfig);

    VkFormat profileFormat =
        VK_FORMAT_R16G16B16A16_SFLOAT;

    VkFormatFeatureFlags requiredFeatures =
        VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT |
        VK_FORMAT_FEATURE_TRANSFER_DST_BIT |
        VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT;

    bool supportsHalf =
        GetDevice().SupportsFormatFeatures(
            profileFormat,
            VK_IMAGE_TILING_OPTIMAL,
            requiredFeatures
        );

    if(supportsHalf){
        std::vector<PackedHalf4> displacementHalf =
            PackHalf4Vector(m_BoreProfileData.displacement);

        std::vector<PackedHalf4> derivativeHalf =
            PackHalf4Vector(m_BoreProfileData.derivative);

        m_BoreProfileDisplacementTexture =
            std::make_unique<water::StaticDataTexture2D>(
                GetPhysicalDevice(),
                GetDevice(),
                GetCommandPool(),
                GetDevice().GetGraphicsQueue(),
                m_BoreProfileData.width,
                m_BoreProfileData.height,
                VK_FORMAT_R16G16B16A16_SFLOAT,
                displacementHalf.data(),
                static_cast<VkDeviceSize>(
                    displacementHalf.size() *
                    sizeof(PackedHalf4)
                )
            );

        m_BoreProfileDerivativeTexture =
            std::make_unique<water::StaticDataTexture2D>(
                GetPhysicalDevice(),
                GetDevice(),
                GetCommandPool(),
                GetDevice().GetGraphicsQueue(),
                m_BoreProfileData.width,
                m_BoreProfileData.height,
                VK_FORMAT_R16G16B16A16_SFLOAT,
                derivativeHalf.data(),
                static_cast<VkDeviceSize>(
                    derivativeHalf.size() *
                    sizeof(PackedHalf4)
                )
            );
    }
    else{
        std::cerr
            << "Warning: RGBA16F linear sampled image unsupported, falling back to RGBA32F\n";

        m_BoreProfileDisplacementTexture =
            std::make_unique<water::StaticDataTexture2D>(
                GetPhysicalDevice(),
                GetDevice(),
                GetCommandPool(),
                GetDevice().GetGraphicsQueue(),
                m_BoreProfileData.width,
                m_BoreProfileData.height,
                VK_FORMAT_R32G32B32A32_SFLOAT,
                m_BoreProfileData.displacement.data(),
                static_cast<VkDeviceSize>(
                    m_BoreProfileData.displacement.size() *
                    sizeof(glm::vec4)
                )
            );

        m_BoreProfileDerivativeTexture =
            std::make_unique<water::StaticDataTexture2D>(
                GetPhysicalDevice(),
                GetDevice(),
                GetCommandPool(),
                GetDevice().GetGraphicsQueue(),
                m_BoreProfileData.width,
                m_BoreProfileData.height,
                VK_FORMAT_R32G32B32A32_SFLOAT,
                m_BoreProfileData.derivative.data(),
                static_cast<VkDeviceSize>(
                    m_BoreProfileData.derivative.size() *
                    sizeof(glm::vec4)
                )
            );
    }

    // m_ProfileTime =
    //     m_BoreProfileConfig.duration *
    //     0.60f; // 固定 Profile 在成熟阶段

    // m_ProfilePaused = true;
    m_ProfileTime = 0.0f;
    m_ProfilePaused = false;
}

void Stage12FluidFluxApp::CreateBoreWakeResources()
{
    uint32_t resolution =
        static_cast<uint32_t>(
            glm::clamp(m_BoreWakeGui.resolution, 256, 2048)
        );

    VkFormat wakeFormat =
        VK_FORMAT_R16G16B16A16_SFLOAT;

    VkFormatFeatureFlags wakeFeatures =
        VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT |
        VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT |
        VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT;

    if(!GetDevice().SupportsFormatFeatures(
        wakeFormat,
        VK_IMAGE_TILING_OPTIMAL,
        wakeFeatures
    )){
        wakeFormat = VK_FORMAT_R32G32B32A32_SFLOAT;
    }

    VkImageUsageFlags wakeUsage =
        VK_IMAGE_USAGE_STORAGE_BIT |
        VK_IMAGE_USAGE_SAMPLED_BIT |
        VK_IMAGE_USAGE_TRANSFER_DST_BIT;

    for(uint32_t i = 0; i < 2; ++i){
        m_BoreWakeStateImages[i] =
            std::make_unique<water::ComputeImage2D>(
                GetPhysicalDevice(),
                GetDevice(),
                resolution,
                resolution,
                wakeFormat,
                wakeUsage
            );
    }

    m_BoreWakeSourceImage =
        std::make_unique<water::ComputeImage2D>(
            GetPhysicalDevice(),
            GetDevice(),
            resolution,
            resolution,
            wakeFormat,
            wakeUsage
        );
}

void Stage12FluidFluxApp::CreateFoamResources()
{
    m_FoamDetailData =
        water::GenerateFoamDetailTexture(
            256,
            256,
            1337u
        );

    m_FoamDetailTexture =
        std::make_unique<water::StaticDataTexture2D>(
            GetPhysicalDevice(),
            GetDevice(),
            GetCommandPool(),
            GetDevice().GetGraphicsQueue(),
            m_FoamDetailData.width,
            m_FoamDetailData.height,
            VK_FORMAT_R8G8B8A8_UNORM,
            m_FoamDetailData.pixels.data(),
            static_cast<VkDeviceSize>(
                m_FoamDetailData.pixels.size() *
                sizeof(water::FoamDetailPixel)
            ),
            true
        );
}

void Stage12FluidFluxApp::CreateFoamStateResources()
{
    VkImageUsageFlags stateUsage =
        VK_IMAGE_USAGE_STORAGE_BIT |
        VK_IMAGE_USAGE_SAMPLED_BIT |
        VK_IMAGE_USAGE_TRANSFER_DST_BIT;

    VkFormat stateFormat =
        VK_FORMAT_R16_SFLOAT;

    VkFormatFeatureFlags stateFeatures =
        VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT |
        VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT |
        VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT;

    if(!GetDevice().SupportsFormatFeatures(
        stateFormat,
        VK_IMAGE_TILING_OPTIMAL,
        stateFeatures
    )){
        stateFormat = VK_FORMAT_R32_SFLOAT;
    }

    for(uint32_t i = 0; i < 2; i++){
        m_FoamStateImages[i] =
            std::make_unique<water::ComputeImage2D>(
                GetPhysicalDevice(),
                GetDevice(),
                m_FoamResolution,
                m_FoamResolution,
                stateFormat,
                stateUsage
            );
    }

    VkFormat sourceFormat =
        VK_FORMAT_R16G16B16A16_SFLOAT;

    VkFormatFeatureFlags sourceFeatures =
        VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT |
        VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT |
        VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT;

    if(!GetDevice().SupportsFormatFeatures(
        sourceFormat,
        VK_IMAGE_TILING_OPTIMAL,
        sourceFeatures
    )){
        sourceFormat = VK_FORMAT_R32G32B32A32_SFLOAT;
    }

    m_FoamSourceVelocityImage =
        std::make_unique<water::ComputeImage2D>(
            GetPhysicalDevice(),
            GetDevice(),
            m_FoamResolution,
            m_FoamResolution,
            sourceFormat,
            VK_IMAGE_USAGE_STORAGE_BIT |
                VK_IMAGE_USAGE_SAMPLED_BIT |
                VK_IMAGE_USAGE_TRANSFER_DST_BIT
        );
}

void Stage12FluidFluxApp::CreateGPUFFTSource()
{
    m_TessendorfGPU = std::make_unique<water::WSTessendorfGPU>(
        GetPhysicalDevice(),
        GetDevice(),
        GetCommandPool(),
        GetDevice().GetGraphicsQueue(),
        GetMaxFramesInFlight(),
        *m_FFTSampler,
        m_OceanConfig.spectrum,
        m_OceanConfig.amplitudeScales
    );
}

void Stage12FluidFluxApp::CreateUniformBuffers()
{
    m_BoreFrontUniformBuffers.clear();
    m_BoreProfileUniformBuffers.clear();
    m_FoamParamsUniformBuffers.clear();
    m_CameraUniformBuffers.clear();
    m_WaterParamsUniformBuffers.clear();
    m_FoamSimulationUniformBuffers.clear();
    m_WaterMaterialUniformBuffers.clear();
    m_MultiBoreUniformBuffers.clear();
    m_BoreEventBuffers.clear();
    m_BoreWakeParamsBuffers.clear();

    m_CameraUniformBuffers.reserve(GetMaxFramesInFlight());
    m_WaterParamsUniformBuffers.reserve(GetMaxFramesInFlight());
    m_BoreFrontUniformBuffers.reserve(GetMaxFramesInFlight());
    m_BoreProfileUniformBuffers.reserve(GetMaxFramesInFlight());
    m_FoamParamsUniformBuffers.reserve(GetMaxFramesInFlight());
    m_FoamSimulationUniformBuffers.reserve(GetMaxFramesInFlight());
    m_WaterMaterialUniformBuffers.reserve(GetMaxFramesInFlight());
    m_MultiBoreUniformBuffers.reserve(GetMaxFramesInFlight());
    m_BoreEventBuffers.reserve(GetMaxFramesInFlight());
    m_BoreWakeParamsBuffers.reserve(GetMaxFramesInFlight());

    // reserve — 只分配内存，不创建对象 resize — 让 vector 包含 n 个默认构造的对象(每帧传 river domain + 潮头 progress)
    m_RiverFieldUniformBuffers.resize(GetMaxFramesInFlight());

    for(uint32_t i = 0; i < GetMaxFramesInFlight(); i++){
        // 创建相机 UBO
        auto cameraBuffer = std::make_unique<vkp::Buffer>(
            GetPhysicalDevice(),
            GetDevice(),
            sizeof(CameraUBO),
            VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
        );
        cameraBuffer->Map();
        m_CameraUniformBuffers.push_back(std::move(cameraBuffer));

        // 创建水体参数 UBO
        auto waterParamsBuffer = std::make_unique<vkp::Buffer>(
            GetPhysicalDevice(),
            GetDevice(),
            sizeof(water::WaterParamsUBO),
            VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
        );
        waterParamsBuffer->Map();
        m_WaterParamsUniformBuffers.push_back(std::move(waterParamsBuffer));

        // 创建 BoreFront UBO
        auto boreFrontBuffer = std::make_unique<vkp::Buffer>(
            GetPhysicalDevice(),
            GetDevice(),
            sizeof(water::BoreFrontUBO),
            VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
        );
        boreFrontBuffer->Map();
        m_BoreFrontUniformBuffers.push_back(std::move(boreFrontBuffer));

        // 创建 BoreProfile UBO
        auto boreProfileBuffer = std::make_unique<vkp::Buffer>(
            GetPhysicalDevice(),
            GetDevice(),
            sizeof(water::BoreProfileUBO),
            VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
        );
        boreProfileBuffer->Map();
        m_BoreProfileUniformBuffers.push_back(std::move(boreProfileBuffer));

        // 创建泡沫参数 UBO
        auto foamParamsBuffer = std::make_unique<vkp::Buffer>(
            GetPhysicalDevice(),
            GetDevice(),
            sizeof(water::FoamParamsUBO),
            VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
        );
        foamParamsBuffer->Map();
        m_FoamParamsUniformBuffers.push_back(std::move(foamParamsBuffer));

        // 创建泡沫模拟 UBO
        auto foamSimulationBuffer = std::make_unique<vkp::Buffer>(
            GetPhysicalDevice(),
            GetDevice(),
            sizeof(water::FoamSimulationUBO),
            VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
        );
        foamSimulationBuffer->Map();
        m_FoamSimulationUniformBuffers.push_back(std::move(foamSimulationBuffer));

        // 创建水体材质 UBO
        auto waterMaterialBuffer = std::make_unique<vkp::Buffer>(
            GetPhysicalDevice(),
            GetDevice(),
            sizeof(water::WaterMaterialUBO),
            VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
        );
        waterMaterialBuffer->Map();
        m_WaterMaterialUniformBuffers.push_back(std::move(waterMaterialBuffer));

        // 创建河流 UBO
        m_RiverFieldUniformBuffers[i] = std::make_unique<vkp::Buffer>(
            GetPhysicalDevice(),
            GetDevice(),
            sizeof(water::RiverFieldUBO),
            VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
        );
        m_RiverFieldUniformBuffers[i]->Map();

        auto multiBoreBuffer = std::make_unique<vkp::Buffer>(
            GetPhysicalDevice(),
            GetDevice(),
            sizeof(water::MultiBoreUBO),
            VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
        );
        multiBoreBuffer->Map();
        m_MultiBoreUniformBuffers.push_back(std::move(multiBoreBuffer));

        auto boreEventBuffer = std::make_unique<vkp::Buffer>(
            GetPhysicalDevice(),
            GetDevice(),
            sizeof(water::BoreEventGPU) * water::kMaxBoreEvents,
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
        );
        boreEventBuffer->Map();
        m_BoreEventBuffers.push_back(std::move(boreEventBuffer));

        auto boreWakeParamsBuffer =
            std::make_unique<vkp::Buffer>(
                GetPhysicalDevice(),
                GetDevice(),
                sizeof(water::BoreWakeParamsUBO),
                VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                    VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
            );
        boreWakeParamsBuffer->Map();
        m_BoreWakeParamsBuffers.push_back(std::move(boreWakeParamsBuffer));
    }
}

// visible tiles 每帧变，使用 host-visible persistent mapped SSBO 最简单
// 把所有可见 Tile 的变换数据打包成一个数组，存进 SSBO
void Stage12FluidFluxApp::CreateTileInstanceBuffers()
{
    m_TileInstanceBuffers.clear();
    m_TileInstanceBuffers.reserve(GetMaxFramesInFlight());

    VkDeviceSize bufferSize =
        sizeof(water::WaterTileGPU) *
        m_MaxVisibleWaterTiles;

    for(uint32_t i = 0; i < GetMaxFramesInFlight(); ++i){
        // 可见 Tile 列表每帧都在变，CPU 需要把新的 WaterTileGPU 数组写进缓冲区
        auto buffer =
            std::make_unique<vkp::Buffer>(
                GetPhysicalDevice(),
                GetDevice(),
                bufferSize,
                VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, // ← 这里决定了它是 Storage Buffer
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                    VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
            );

        // Map() 后不 Unmap()，CPU 持有映射指针，每帧直接 memcpy 新数据进去，省去反复映射的开销
        buffer->Map();

        m_TileInstanceBuffers.push_back(
            std::move(buffer)
        );
    }
}

void Stage12FluidFluxApp::CreateCrestRibbonResources()
{
    const uint32_t maxLateralSegments = 512;
    const uint32_t maxDepthSegments = 8;

    m_CrestRibbonVertexCapacity =
        water::kMaxBoreEvents *
        maxLateralSegments *
        maxDepthSegments *
        6;

    m_CrestRibbonVertexBuffers.clear();
    m_CrestRibbonVertexBuffers.reserve(GetMaxFramesInFlight());

    VkDeviceSize bufferSize =
        sizeof(CrestRibbonVertex) *
        m_CrestRibbonVertexCapacity;

    for(uint32_t i = 0; i < GetMaxFramesInFlight(); ++i){
        auto buffer =
            std::make_unique<vkp::Buffer>(
                GetPhysicalDevice(),
                GetDevice(),
                bufferSize,
                VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                    VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
            );

        buffer->Map();
        m_CrestRibbonVertexBuffers.push_back(std::move(buffer));
    }
}

void Stage12FluidFluxApp::CreateDescriptorPool()
{
    m_DescriptorPool = vkp::DescriptorPool::Builder(GetDevice())
        .SetMaxSets(GetMaxFramesInFlight())
        .AddPoolSize(
            VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
            GetMaxFramesInFlight() * 6
        )
        .AddPoolSize(
            VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            GetMaxFramesInFlight() * 14
        )
        .AddPoolSize(
            VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            GetMaxFramesInFlight() * 2
        )
        .Build();
}

void Stage12FluidFluxApp::CreateAppearanceDescriptorPool()
{
    m_AppearanceDescriptorPool =
        vkp::DescriptorPool::Builder(GetDevice())
            .SetMaxSets(GetMaxFramesInFlight())
            .AddPoolSize(
                VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                GetMaxFramesInFlight() * 2
            )
            .AddPoolSize(
                VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                GetMaxFramesInFlight() * 5
            )
            .Build();
}

void Stage12FluidFluxApp::CreateFoamComputeDescriptorPool()
{
    m_FoamComputeDescriptorPool =
        vkp::DescriptorPool::Builder(GetDevice())
            .SetMaxSets(GetMaxFramesInFlight() * 3)
            .AddPoolSize(
                VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                GetMaxFramesInFlight() * 9
            )
            .AddPoolSize(
                VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                GetMaxFramesInFlight() * 17
            )
            .AddPoolSize(
                VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                GetMaxFramesInFlight()
            )
            .AddPoolSize(
                VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
                GetMaxFramesInFlight() * 3
            )
            .Build();
}

void Stage12FluidFluxApp::CreateBoreWakeDescriptorPool()
{
    m_BoreWakeDescriptorPool =
        vkp::DescriptorPool::Builder(GetDevice())
            .SetMaxSets(GetMaxFramesInFlight() * 3)
            .AddPoolSize(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, GetMaxFramesInFlight() * 5)
            .AddPoolSize(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, GetMaxFramesInFlight())
            .AddPoolSize(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, GetMaxFramesInFlight() * 9)
            .AddPoolSize(VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, GetMaxFramesInFlight() * 3)
            .Build();
}

// 为每一个飞行帧分配描述符集，并将具体的缓冲区和纹理绑定到着色器的槽位上。
// 负责创建图形着色器（顶点/片段着色器） 使用的描述符集
// 用途：为水面渲染绑定摄像机矩阵、水体参数、位移图、法线辅助图等资源。
// 绑定内容：CameraUBO、WaterParamsUBO、fftDisplacement0~2（三层的位移纹理）、fftNormalAux0~2（三层的法线辅助纹理）等。
// 着色器类型：顶点着色器（.vert）、片段着色器（.frag）。
// 描述符集布局：m_DescriptorSetLayout（在 Stage10RiverWaterApp 中创建，与计算管线的布局不同）。
void Stage12FluidFluxApp::CreateDescriptorSets()
{
    // 为每个飞行帧预留一个描述符集的空位
    m_DescriptorSets.resize(GetMaxFramesInFlight());

    // 遍历每一个飞行帧，逐个填充描述符集
    for(uint32_t i = 0; i < GetMaxFramesInFlight(); i++){
        
        // 1. 准备 camera UBO 的绑定信息
        VkDescriptorBufferInfo cameraInfo{};
        cameraInfo.buffer = *m_CameraUniformBuffers[i]; // 指向当前帧的相机 UBO
        cameraInfo.offset = 0;
        cameraInfo.range = sizeof(CameraUBO);           // 绑定整个 UBO

        // 2. 准备 水体参数 UBO 的绑定信息
        VkDescriptorBufferInfo waterParamsInfo{};
        waterParamsInfo.buffer = *m_WaterParamsUniformBuffers[i]; // 指向当前帧的水体参数 UBO
        waterParamsInfo.offset = 0;
        waterParamsInfo.range = sizeof(water::WaterParamsUBO);   // 绑定整个 UBO

        VkDescriptorBufferInfo boreFrontInfo{};
        boreFrontInfo.buffer = *m_BoreFrontUniformBuffers[i];
        boreFrontInfo.offset = 0;
        boreFrontInfo.range = sizeof(water::BoreFrontUBO);

        VkDescriptorBufferInfo boreProfileInfo{};
        boreProfileInfo.buffer = *m_BoreProfileUniformBuffers[i];
        boreProfileInfo.offset = 0;
        boreProfileInfo.range = sizeof(water::BoreProfileUBO);

        VkDescriptorBufferInfo tileInstanceInfo{};
        tileInstanceInfo.buffer = *m_TileInstanceBuffers[i];
        tileInstanceInfo.offset = 0;
        tileInstanceInfo.range =
            sizeof(water::WaterTileGPU) *
            m_MaxVisibleWaterTiles;

        VkDescriptorBufferInfo riverFieldInfo{};
        riverFieldInfo.buffer = *m_RiverFieldUniformBuffers[i];
        riverFieldInfo.offset = 0;
        riverFieldInfo.range = sizeof(water::RiverFieldUBO);

        VkDescriptorBufferInfo multiBoreInfo{};
        multiBoreInfo.buffer = *m_MultiBoreUniformBuffers[i];
        multiBoreInfo.offset = 0;
        multiBoreInfo.range = sizeof(water::MultiBoreUBO);

        VkDescriptorBufferInfo boreEventInfo{};
        boreEventInfo.buffer = *m_BoreEventBuffers[i];
        boreEventInfo.offset = 0;
        boreEventInfo.range = sizeof(water::BoreEventGPU) * water::kMaxBoreEvents;

        // 3. 准备 位移图 的绑定信息。
        //    GetDescriptorInfo 内部会封装 VkImageView 和 VkSampler，
        //    并指定图像布局为 SHADER_READ_ONLY_OPTIMAL，供着色器采样
        // 4. 准备 法线辅助图 的绑定信息，同样使用 FFTSampler
        water::WaterSurfaceGPUResources gpuResources =
            m_TessendorfGPU->GetGPUResources(i);

        VkDescriptorImageInfo frontParameterInfo =
            m_FrontParameterTexture->GetDescriptorInfo(*m_FrontLUTSampler);

        VkDescriptorImageInfo frontDerivativeInfo =
            m_FrontDerivativeTexture->GetDescriptorInfo(*m_FrontLUTSampler);

        VkDescriptorImageInfo boreProfileDisplacementInfo =
            m_BoreProfileDisplacementTexture->GetDescriptorInfo(
                *m_BoreProfileSampler
            );

        VkDescriptorImageInfo boreProfileDerivativeInfo =
            m_BoreProfileDerivativeTexture->GetDescriptorInfo(
                *m_BoreProfileSampler
            );

        VkDescriptorImageInfo riverFlowInfo =
            m_RiverFlowTexture->GetDescriptorInfo(
                *m_RiverSampler
            );

        VkDescriptorImageInfo riverCoordinateInfo =
            m_RiverCoordinateTexture->GetDescriptorInfo(
                *m_RiverSampler
            );

        VkDescriptorImageInfo progressFieldInfo =
            m_ProgressFieldTexture->GetDescriptorInfo(
                *m_RiverSampler
            );

        VkDescriptorImageInfo shoreMaskInfo =
            m_ShoreMaskTexture->GetDescriptorInfo(
                *m_RiverSampler
            );

        // 5. 用 DescriptorWriter 将上述资源按顺序写入描述符集
        //    binding 0：camera UBO 每一帧，在 UpdateCameraUniformBuffer 中生成视图矩阵 投影矩阵 着色器通过 binding = 0 读取这个 UBO，完成顶点到裁剪空间的变换
        //    binding 1：water params UBO 存储控制波浪变形的全局参数(FFT 分辨率 补丁长度 choppy 强度 调试模式标志)
        //    binding 2：位移图（组合图像采样器）
        //    binding 3：法线辅助图（组合图像采样器）
        bool success = vkp::DescriptorWriter(*m_DescriptorSetLayout, *m_DescriptorPool)
            .WriteBuffer(0, &cameraInfo)
            .WriteBuffer(1, &waterParamsInfo)
            .WriteImage(2, &gpuResources.cascades[0].displacement)
            .WriteImage(3, &gpuResources.cascades[0].normalAux)
            .WriteImage(4, &gpuResources.cascades[1].displacement)
            .WriteImage(5, &gpuResources.cascades[1].normalAux)
            .WriteImage(6, &gpuResources.cascades[2].displacement)
            .WriteImage(7, &gpuResources.cascades[2].normalAux)
            .WriteBuffer(8, &boreFrontInfo)
            .WriteImage(9, &frontParameterInfo)
            .WriteImage(10, &frontDerivativeInfo)
            .WriteBuffer(11, &boreProfileInfo)
            .WriteImage(12, &boreProfileDisplacementInfo)
            .WriteImage(13, &boreProfileDerivativeInfo)
            .WriteBuffer(14, &tileInstanceInfo)
            .WriteBuffer(15, &riverFieldInfo)
            .WriteImage(16, &riverFlowInfo)
            .WriteImage(17, &riverCoordinateInfo)
            .WriteBuffer(18, &multiBoreInfo)
            .WriteBuffer(19, &boreEventInfo)
            .WriteImage(20, &progressFieldInfo)
            .WriteImage(21, &shoreMaskInfo)
            .Build(m_DescriptorSets[i]); // 完成分配与写入

        // 如果分配或写入失败（比如池子空间不足），立即抛出异常
        if(!success){
            throw std::runtime_error("Failed to allocate Stage6 GPU FFT descriptor set");
        }
    }
}

void Stage12FluidFluxApp::CreateAppearanceDescriptorSets()
{
    m_AppearanceDescriptorSets.resize(GetMaxFramesInFlight());

    for(uint32_t i = 0; i < GetMaxFramesInFlight(); i++){
        VkDescriptorBufferInfo foamParamsInfo{};
        foamParamsInfo.buffer = *m_FoamParamsUniformBuffers[i];
        foamParamsInfo.offset = 0;
        foamParamsInfo.range = sizeof(water::FoamParamsUBO);

        VkDescriptorImageInfo foamDetailInfo =
            m_FoamDetailTexture->GetDescriptorInfo(
                *m_FoamDetailSampler
            );

        VkDescriptorImageInfo foamState0Info =
            m_FoamStateImages[0]->GetSampledDescriptorInfo(
                *m_FoamStateSampler
            );

        VkDescriptorImageInfo foamState1Info =
            m_FoamStateImages[1]->GetSampledDescriptorInfo(
                *m_FoamStateSampler
            );

        VkDescriptorImageInfo boreWakeState0Info =
            m_BoreWakeStateImages[0]->GetSampledDescriptorInfo(
                *m_FoamStateSampler
            );

        VkDescriptorImageInfo boreWakeState1Info =
            m_BoreWakeStateImages[1]->GetSampledDescriptorInfo(
                *m_FoamStateSampler
            );

        VkDescriptorBufferInfo waterMaterialInfo{};
        waterMaterialInfo.buffer = *m_WaterMaterialUniformBuffers[i];
        waterMaterialInfo.offset = 0;
        waterMaterialInfo.range = sizeof(water::WaterMaterialUBO);

        bool success =
            vkp::DescriptorWriter(
                *m_AppearanceDescriptorSetLayout,
                *m_AppearanceDescriptorPool
            )
                .WriteBuffer(0, &foamParamsInfo)
                .WriteImage(1, &foamDetailInfo)
                .WriteImage(2, &foamState0Info)
                .WriteImage(3, &foamState1Info)
                .WriteBuffer(4, &waterMaterialInfo)
                .WriteImage(5, &boreWakeState0Info)
                .WriteImage(6, &boreWakeState1Info)
                .Build(m_AppearanceDescriptorSets[i]);

        if(!success){
            throw std::runtime_error("Failed to allocate Stage9 appearance descriptor set");
        }
    }
}

// 创建泡沫 Compute Shader 所需的全部描述符集
    // 为泡沫源计算 (foam_source.comp) 创建描述符集：类型为 m_FoamSourceSets，每个飞行帧一个，
    // 共 GetMaxFramesInFlight() 个。这些描述符集包含了计算该帧泡沫源所需的全部资源。
    // 为泡沫平流计算 (foam_advect.comp) 创建描述符集：类型为 m_FoamAdvectSets，固定只有 2 套（不是按飞行帧分的）。
    // 这两套正好对应 Ping-Pong 状态切换所需的两种配置——读状态0写状态1，或读状态1写状态0。
void Stage12FluidFluxApp::CreateFoamComputeDescriptorSets()
{
    m_FoamSourceSets.resize(GetMaxFramesInFlight());
    m_FoamAdvectSets.resize(GetMaxFramesInFlight());

    VkDescriptorImageInfo frontParameterInfo =
        m_FrontParameterTexture->GetDescriptorInfo(*m_FrontLUTSampler);

    VkDescriptorImageInfo frontDerivativeInfo =
        m_FrontDerivativeTexture->GetDescriptorInfo(*m_FrontLUTSampler);

    VkDescriptorImageInfo profileDisplacementInfo =
        m_BoreProfileDisplacementTexture->GetDescriptorInfo(*m_BoreProfileSampler);

    VkDescriptorImageInfo profileDerivativeInfo =
        m_BoreProfileDerivativeTexture->GetDescriptorInfo(*m_BoreProfileSampler);

    VkDescriptorImageInfo sourceVelocityStorageInfo =
        m_FoamSourceVelocityImage->GetStorageDescriptorInfo();

    for(uint32_t i = 0; i < GetMaxFramesInFlight(); i++){
        VkDescriptorBufferInfo foamSimulationInfo{};
        foamSimulationInfo.buffer = *m_FoamSimulationUniformBuffers[i];
        foamSimulationInfo.offset = 0;
        foamSimulationInfo.range = sizeof(water::FoamSimulationUBO);

        VkDescriptorBufferInfo foamParamsInfo{};
        foamParamsInfo.buffer = *m_FoamParamsUniformBuffers[i];
        foamParamsInfo.offset = 0;
        foamParamsInfo.range = sizeof(water::FoamParamsUBO);

        VkDescriptorBufferInfo boreFrontInfo{};
        boreFrontInfo.buffer = *m_BoreFrontUniformBuffers[i];
        boreFrontInfo.offset = 0;
        boreFrontInfo.range = sizeof(water::BoreFrontUBO);

        VkDescriptorBufferInfo boreProfileInfo{};
        boreProfileInfo.buffer = *m_BoreProfileUniformBuffers[i];
        boreProfileInfo.offset = 0;
        boreProfileInfo.range = sizeof(water::BoreProfileUBO);

        VkDescriptorBufferInfo waterParamsInfo{};
        waterParamsInfo.buffer = *m_WaterParamsUniformBuffers[i];
        waterParamsInfo.offset = 0;
        waterParamsInfo.range = sizeof(water::WaterParamsUBO);

        VkDescriptorBufferInfo multiBoreInfo{};
        multiBoreInfo.buffer = *m_MultiBoreUniformBuffers[i];
        multiBoreInfo.offset = 0;
        multiBoreInfo.range = sizeof(water::MultiBoreUBO);

        VkDescriptorBufferInfo boreEventInfo{};
        boreEventInfo.buffer = *m_BoreEventBuffers[i];
        boreEventInfo.offset = 0;
        boreEventInfo.range = sizeof(water::BoreEventGPU) * water::kMaxBoreEvents;

        VkDescriptorImageInfo riverFlowInfo =
            m_RiverFlowTexture->GetDescriptorInfo(*m_RiverSampler);

        VkDescriptorImageInfo riverCoordinateInfo =
            m_RiverCoordinateTexture->GetDescriptorInfo(*m_RiverSampler);

        VkDescriptorImageInfo progressFieldInfo =
            m_ProgressFieldTexture->GetDescriptorInfo(*m_RiverSampler);

        water::WaterSurfaceGPUResources gpuResources =
            m_TessendorfGPU->GetGPUResources(i);

        bool success =
            vkp::DescriptorWriter(
                *m_FoamSourceSetLayout,
                *m_FoamComputeDescriptorPool
            )
                .WriteBuffer(0, &foamSimulationInfo)
                .WriteBuffer(1, &foamParamsInfo)
                .WriteBuffer(2, &boreFrontInfo)
                .WriteBuffer(3, &boreProfileInfo)
                .WriteImage(4, &frontParameterInfo)
                .WriteImage(5, &frontDerivativeInfo)
                .WriteImage(6, &profileDisplacementInfo)
                .WriteImage(7, &profileDerivativeInfo)
                .WriteImage(8, &gpuResources.cascades[0].displacement)
                .WriteImage(9, &gpuResources.cascades[0].normalAux)
                .WriteImage(10, &gpuResources.cascades[1].displacement)
                .WriteImage(11, &gpuResources.cascades[1].normalAux)
                .WriteImage(12, &gpuResources.cascades[2].displacement)
                .WriteImage(13, &gpuResources.cascades[2].normalAux)
                .WriteBuffer(14, &waterParamsInfo)
                .WriteImage(15, &sourceVelocityStorageInfo)
                .WriteBuffer(16, &multiBoreInfo)
                .WriteBuffer(17, &boreEventInfo)
                .WriteImage(18, &riverFlowInfo)
                .WriteImage(19, &riverCoordinateInfo)
                .WriteImage(20, &progressFieldInfo)
                .Build(m_FoamSourceSets[i]);

        if(!success){
            throw std::runtime_error("Failed to allocate foam source descriptor set");
        }
    }

    for(uint32_t frameIndex = 0; frameIndex < GetMaxFramesInFlight(); frameIndex++){
        for(uint32_t ping = 0; ping < 2; ping++){
            uint32_t readIndex =
                ping;

            uint32_t writeIndex =
                1 - ping;

            VkDescriptorBufferInfo foamSimulationInfo{};
            foamSimulationInfo.buffer =
                *m_FoamSimulationUniformBuffers[frameIndex];
            foamSimulationInfo.offset = 0;
            foamSimulationInfo.range = sizeof(water::FoamSimulationUBO);

            VkDescriptorImageInfo previousFoamInfo =
                m_FoamStateImages[readIndex]->GetSampledDescriptorInfo(
                    *m_FoamStateSampler
                );

            VkDescriptorImageInfo sourceVelocityInfo =
                m_FoamSourceVelocityImage->GetSampledDescriptorInfo(
                    *m_FoamStateSampler
                );

            VkDescriptorImageInfo nextFoamInfo =
                m_FoamStateImages[writeIndex]->GetStorageDescriptorInfo();

            VkDescriptorImageInfo riverFlowInfo =
                m_RiverFlowTexture->GetDescriptorInfo(*m_RiverSampler);

            VkDescriptorBufferInfo multiBoreInfo{};
            multiBoreInfo.buffer = *m_MultiBoreUniformBuffers[frameIndex];
            multiBoreInfo.offset = 0;
            multiBoreInfo.range = sizeof(water::MultiBoreUBO);

            bool success =
                vkp::DescriptorWriter(
                    *m_FoamAdvectSetLayout,
                    *m_FoamComputeDescriptorPool
                )
                    .WriteBuffer(0, &foamSimulationInfo)
                    .WriteImage(1, &previousFoamInfo)
                    .WriteImage(2, &sourceVelocityInfo)
                    .WriteImage(3, &nextFoamInfo)
                    .WriteImage(4, &riverFlowInfo)
                    .WriteBuffer(5, &multiBoreInfo)
                    .Build(m_FoamAdvectSets[frameIndex][ping]);

            if(!success){
                throw std::runtime_error("Failed to allocate foam advect descriptor set");
            }
        }
    }
}

void Stage12FluidFluxApp::CreateBoreWakeDescriptorSets()
{
    m_BoreWakeSourceSets.resize(GetMaxFramesInFlight());
    m_BoreWakeAdvectSets.resize(GetMaxFramesInFlight());

    for(uint32_t i = 0; i < GetMaxFramesInFlight(); ++i){
        VkDescriptorBufferInfo wakeParamsInfo{};
        wakeParamsInfo.buffer = *m_BoreWakeParamsBuffers[i];
        wakeParamsInfo.offset = 0;
        wakeParamsInfo.range = sizeof(water::BoreWakeParamsUBO);

        VkDescriptorBufferInfo multiBoreInfo{};
        multiBoreInfo.buffer = *m_MultiBoreUniformBuffers[i];
        multiBoreInfo.offset = 0;
        multiBoreInfo.range = sizeof(water::MultiBoreUBO);

        VkDescriptorBufferInfo boreEventInfo{};
        boreEventInfo.buffer = *m_BoreEventBuffers[i];
        boreEventInfo.offset = 0;
        boreEventInfo.range = sizeof(water::BoreEventGPU) * water::kMaxBoreEvents;

        VkDescriptorImageInfo riverFlowInfo =
            m_RiverFlowTexture->GetDescriptorInfo(*m_RiverSampler);

            VkDescriptorImageInfo progressFieldInfo =
                m_ProgressFieldTexture->GetDescriptorInfo(*m_RiverSampler);

            VkDescriptorImageInfo riverCoordinateInfo =
                m_RiverCoordinateTexture->GetDescriptorInfo(*m_RiverSampler);

            VkDescriptorImageInfo sourceInfo =
                m_BoreWakeSourceImage->GetStorageDescriptorInfo();

        bool success =
            vkp::DescriptorWriter(*m_BoreWakeSourceSetLayout, *m_BoreWakeDescriptorPool)
                .WriteBuffer(0, &wakeParamsInfo)
                .WriteBuffer(1, &multiBoreInfo)
                .WriteBuffer(2, &wakeParamsInfo)
                .WriteBuffer(3, &boreEventInfo)
                .WriteImage(4, &riverFlowInfo)
                .WriteImage(5, &riverCoordinateInfo)
                .WriteImage(6, &progressFieldInfo)
                .WriteImage(7, &sourceInfo)
                .Build(m_BoreWakeSourceSets[i]);

        if(!success){
            throw std::runtime_error("Failed to allocate BoreWake source descriptor set");
        }
    }

    for(uint32_t frameIndex = 0; frameIndex < GetMaxFramesInFlight(); ++frameIndex){
        for(uint32_t ping = 0; ping < 2; ++ping){
            uint32_t readIndex = ping;
            uint32_t writeIndex = 1 - ping;

            VkDescriptorBufferInfo wakeParamsInfo{};
            wakeParamsInfo.buffer = *m_BoreWakeParamsBuffers[frameIndex];
            wakeParamsInfo.offset = 0;
            wakeParamsInfo.range = sizeof(water::BoreWakeParamsUBO);

            VkDescriptorImageInfo previousInfo =
                m_BoreWakeStateImages[readIndex]->GetSampledDescriptorInfo(*m_FoamStateSampler);

            VkDescriptorImageInfo sourceInfo =
                m_BoreWakeSourceImage->GetSampledDescriptorInfo(*m_FoamStateSampler);

            VkDescriptorImageInfo nextInfo =
                m_BoreWakeStateImages[writeIndex]->GetStorageDescriptorInfo();

            VkDescriptorImageInfo riverCoordinateInfo =
                m_RiverCoordinateTexture->GetDescriptorInfo(*m_RiverSampler);

            bool success =
                vkp::DescriptorWriter(*m_BoreWakeAdvectSetLayout, *m_BoreWakeDescriptorPool)
                    .WriteBuffer(0, &wakeParamsInfo)
                    .WriteImage(1, &previousInfo)
                    .WriteImage(2, &sourceInfo)
                    .WriteImage(3, &nextInfo)
                    .WriteImage(4, &riverCoordinateInfo)
                    .Build(m_BoreWakeAdvectSets[frameIndex][ping]);

            if(!success){
                throw std::runtime_error("Failed to allocate BoreWake advect descriptor set");
            }
        }
    }
}

// 每帧录制泡沫计算的全部命令
// 这是泡沫系统的核心调度入口。它将源项生成和平流求解串成一个完整的 Compute Pass，并负责 Ping-Pong 状态的翻转。
    // 确定读写索引：根据当前泡沫状态索引 m_CurrentFoamStateIndex，算出哪张状态图是读（上一帧的结果），哪张是写（将要生成的新状态）。
    // 第一步：插入前置屏障
        // 将要被写入的图片（源图、写状态图）从“片段着色器可读”状态转换为“计算着色器可写”状态，避免数据竞争。
    // 第二步：执行泡沫源计算 (foam_source.comp)
        // 绑定源描述符集（按帧索引），调度工作组。
        // 这一步会将当前帧的泡沫源和流速写入 m_FoamSourceVelocityImage。
    // 第三步：插入中间屏障
        // 确保源写入完全对后续的平流计算可见。
    // 第四步：执行泡沫平流计算 (foam_advect.comp)
        // 绑定平流描述符集（用读索引来选择 m_FoamAdvectSets[readIndex]），
        // 也就是决定了“从哪张图读历史，向哪张图写新状态”。
        // 调度工作组。这一步会结合源、流速和上一帧泡沫，生成新一帧的泡沫状态图。
    // 第五步：插入后置屏障
        // 将刚写入的新状态图从“计算可写”转换为“片段可读”，这样后续的 Graphics Pass 才能正确采样新泡沫。
    // 翻转 Ping-Pong 索引：m_CurrentFoamStateIndex = writeIndex。下一次调用时，读/写对象自动互换。
void Stage12FluidFluxApp::RecordFoamSimulation(
    VkCommandBuffer commandBuffer,
    uint32_t frameIndex
)
{
    uint32_t readIndex =
        m_CurrentFoamStateIndex;

    uint32_t writeIndex =
        1 - readIndex;

    m_FoamSourceVelocityImage->RecordComputeReadToComputeWriteBarrier(commandBuffer);
    m_FoamStateImages[writeIndex]->RecordFragmentReadToComputeWriteBarrier(commandBuffer);

    vkCmdBindPipeline(
        commandBuffer,
        VK_PIPELINE_BIND_POINT_COMPUTE,
        m_FoamSourcePipeline->GetHandle()
    );

    vkCmdBindDescriptorSets(
        commandBuffer,
        VK_PIPELINE_BIND_POINT_COMPUTE,
        m_FoamSourcePipeline->GetLayout(),
        0,
        1,
        &m_FoamSourceSets[frameIndex],
        0,
        nullptr
    );

    uint32_t groups =
        (m_FoamResolution + 7) / 8;

    vkCmdDispatch(
        commandBuffer,
        groups,
        groups,
        1
    );

    m_FoamSourceVelocityImage->RecordComputeWriteToComputeReadBarrier(commandBuffer);

    vkCmdBindPipeline(
        commandBuffer,
        VK_PIPELINE_BIND_POINT_COMPUTE,
        m_FoamAdvectPipeline->GetHandle()
    );

    vkCmdBindDescriptorSets(
        commandBuffer,
        VK_PIPELINE_BIND_POINT_COMPUTE,
        m_FoamAdvectPipeline->GetLayout(),
        0,
        1,
        &m_FoamAdvectSets[frameIndex][readIndex],
        0,
        nullptr
    );

    vkCmdDispatch(
        commandBuffer,
        groups,
        groups,
        1
    );

    m_FoamStateImages[writeIndex]->RecordComputeWriteToFragmentReadBarrier(commandBuffer);

    m_CurrentFoamStateIndex =
        writeIndex;
}


// 每帧录制涌潮尾流泡沫模拟的全部命令
// 这是涌潮尾流泡沫系统的核心调度入口。它将源项生成和状态积累/衰减串成一个完整的 Compute Pass，并负责 Ping-Pong 状态的翻转。
    // 确定读写索引：根据当前尾流状态索引 m_CurrentBoreWakeStateIndex，算出哪张状态图是读（上一帧的结果），哪张是写（将要生成的新状态）。
    // 第一步：插入前置屏障
        // 将要被写入的图片（源图、写状态图）从“片段着色器可读/计算着色器已读”状态转换为“计算着色器可写”状态，避免数据竞争。
    // 第二步：执行涌潮尾流源计算 (bore_wake_source.comp)
        // 绑定源描述符集（按帧索引），调度工作组。
        // 这一步会根据当前涌潮事件位置和参数计算出尾流泡沫源，写入 m_BoreWakeSourceImage。
    // 第三步：插入中间屏障
        // 确保源写入完全对后续的状态更新计算可见。
    // 第四步：执行涌潮尾流状态更新 (bore_wake_advect.comp)
        // 绑定状态更新描述符集（用读索引来选择 m_BoreWakeAdvectSets[frameIndex][readIndex]），
        // 也就是决定了“从哪张图读历史状态，向哪张图写新状态”。
        // 调度工作组。这一步会结合本帧源项、上一帧尾流状态、衰减和扩散，生成新一帧的尾流状态图。
    // 第五步：插入后置屏障
        // 将刚写入的新状态图从“计算可写”转换为“片段可读”，这样后续的 Graphics Pass 才能正确采样新尾流泡沫。
    // 翻转 Ping-Pong 索引：m_CurrentBoreWakeStateIndex = writeIndex。下一次调用时，读/写对象自动互换。
void Stage12FluidFluxApp::RecordBoreWakeSimulation(
    VkCommandBuffer commandBuffer,
    uint32_t frameIndex
)
{
    // 如果尾流效果未启用，直接返回，不录制任何命令
    if(!m_BoreWakeGui.enabled){
        return;
    }

    uint32_t readIndex = m_CurrentBoreWakeStateIndex;
    uint32_t writeIndex = 1 - readIndex;

    // ---------- 1. 前置屏障：准备写入源图和目标状态图 ----------
    // 将源图从之前的读取状态转为可写入状态
    m_BoreWakeSourceImage->RecordComputeReadToComputeWriteBarrier(commandBuffer);
    // 将要写入的状态图从片段着色器可读（上一帧渲染可能还在用）转为计算着色器可写
    m_BoreWakeStateImages[writeIndex]->RecordFragmentReadToComputeWriteBarrier(commandBuffer);

    // ---------- 2. 源项生成：bore_wake_source.comp ----------
    vkCmdBindPipeline(
        commandBuffer,
        VK_PIPELINE_BIND_POINT_COMPUTE,
        m_BoreWakeSourcePipeline->GetHandle()
    );

    vkCmdBindDescriptorSets(
        commandBuffer,
        VK_PIPELINE_BIND_POINT_COMPUTE,
        m_BoreWakeSourcePipeline->GetLayout(),
        0,
        1,
        &m_BoreWakeSourceSets[frameIndex],
        0,
        nullptr
    );

    // 计算工作组数量（8x8 线程/组）
    uint32_t groups =
        (static_cast<uint32_t>(m_BoreWakeGui.resolution) + 7) / 8;
    vkCmdDispatch(commandBuffer, groups, groups, 1);

    // ---------- 3. 中间屏障：源写入 → 状态更新读取 ----------
    // 确保源图已完成写入，后续状态更新着色器可以安全读取
    m_BoreWakeSourceImage->RecordComputeWriteToComputeReadBarrier(commandBuffer);

    // ---------- 4. 状态更新：bore_wake_advect.comp ----------
    vkCmdBindPipeline(
        commandBuffer,
        VK_PIPELINE_BIND_POINT_COMPUTE,
        m_BoreWakeAdvectPipeline->GetHandle()
    );

    // 使用读索引对应的描述符集，决定“从哪张状态图读，向哪张状态图写”
    vkCmdBindDescriptorSets(
        commandBuffer,
        VK_PIPELINE_BIND_POINT_COMPUTE,
        m_BoreWakeAdvectPipeline->GetLayout(),
        0,
        1,
        &m_BoreWakeAdvectSets[frameIndex][readIndex],
        0,
        nullptr
    );

    vkCmdDispatch(commandBuffer, groups, groups, 1);

    // ---------- 5. 后置屏障：状态图写入 → 片段着色器可读 ----------
    // 将刚写入的新状态图从计算可写转为片段可读，后续水面渲染可以采样它
    m_BoreWakeStateImages[writeIndex]->RecordComputeWriteToFragmentReadBarrier(commandBuffer);

    // ---------- 6. 翻转 Ping-Pong 索引 ----------
    m_CurrentBoreWakeStateIndex = writeIndex;
}


void Stage12FluidFluxApp::InitializeFoamStateImages()
{
    VkCommandBuffer commandBuffer =
        GetCommandPool().BeginOneTimeCommands(GetDevice());

    m_FoamStateImages[0]->RecordTransitionToGeneral(commandBuffer);
    m_FoamStateImages[1]->RecordTransitionToGeneral(commandBuffer);
    m_FoamSourceVelocityImage->RecordTransitionToGeneral(commandBuffer);

    m_FoamStateImages[0]->RecordClear(commandBuffer, 0.0f);
    m_FoamStateImages[1]->RecordClear(commandBuffer, 0.0f);
    m_FoamSourceVelocityImage->RecordClear(commandBuffer, 0.0f);

    GetCommandPool().EndOneTimeCommands(
        GetDevice(),
        GetDevice().GetGraphicsQueue(),
        commandBuffer
    );
}

void Stage12FluidFluxApp::InitializeBoreWakeImages()
{
    VkCommandBuffer commandBuffer =
        GetCommandPool().BeginOneTimeCommands(GetDevice());

    m_BoreWakeStateImages[0]->RecordTransitionToGeneral(commandBuffer);
    m_BoreWakeStateImages[1]->RecordTransitionToGeneral(commandBuffer);
    m_BoreWakeSourceImage->RecordTransitionToGeneral(commandBuffer);

    m_BoreWakeStateImages[0]->RecordClear(commandBuffer, 0.0f);
    m_BoreWakeStateImages[1]->RecordClear(commandBuffer, 0.0f);
    m_BoreWakeSourceImage->RecordClear(commandBuffer, 0.0f);

    GetCommandPool().EndOneTimeCommands(
        GetDevice(),
        GetDevice().GetGraphicsQueue(),
        commandBuffer
    );
}

void Stage12FluidFluxApp::UpdateCamera(float deltaTime)
{
    float moveSpeed = 320.0f;

    if(m_Keys[GLFW_KEY_LEFT_SHIFT]){
        moveSpeed = 640.0f;
    }

    if(m_Keys[GLFW_KEY_LEFT_ALT]){
        moveSpeed = 40.0f;
    }

    float safeDeltaTime =
        std::min(deltaTime, 0.05f);

    float distance =
        moveSpeed * safeDeltaTime;

    if(m_Keys[GLFW_KEY_W]){
        m_Camera.MoveForward(distance);
    }

    if(m_Keys[GLFW_KEY_S]){
        m_Camera.MoveForward(-distance);
    }

    if(m_Keys[GLFW_KEY_A]){
        m_Camera.MoveRight(-distance);
    }

    if(m_Keys[GLFW_KEY_D]){
        m_Camera.MoveRight(distance);
    }

    if(m_Keys[GLFW_KEY_SPACE]){
        m_Camera.MoveUp(distance);
    }

    if(m_Keys[GLFW_KEY_LEFT_CONTROL]){
        m_Camera.MoveUp(-distance);
    }
}

void Stage12FluidFluxApp::Update(core::Timestep timestep)
{
    float deltaTime = timestep.GetSeconds();

    UpdateCamera(deltaTime);

    float simulationDeltaTime = 0.0f;

    if(!m_Paused){
        simulationDeltaTime = deltaTime;
    }

    if(m_StepOnce){
        simulationDeltaTime = 1.0f / 60.0f;
        m_StepOnce = false;
    }

    m_LastSimulationDeltaTime = simulationDeltaTime;
    m_Time += simulationDeltaTime;

    float boreDeltaTime = 0.0f;

    if(!m_BorePaused && !m_Paused){
        float clampedDeltaTime =
            std::min(deltaTime, 0.10f);

        constexpr float fixedStep = 1.0f / 60.0f;
        constexpr int maxSubsteps = 8;

        m_BoreAccumulator += clampedDeltaTime;

        int substepCount = 0;
        while(m_BoreAccumulator >= fixedStep && substepCount < maxSubsteps){
            m_BoreEventManager.Update(
                fixedStep,
                m_RiverLength,
                m_BoreProfileConfig.profileHalfWidth,
                BuildBoreEventManagerConfig()
            );

            m_BoreTime += fixedStep;
            boreDeltaTime += fixedStep;
            m_BoreAccumulator -= fixedStep;
            ++substepCount;
        }

        if(substepCount == maxSubsteps){
            m_BoreAccumulator = std::min(m_BoreAccumulator, fixedStep);
        }

        // 历史高水位标记：只增不减，事件被移除后水位不回落
        for(const water::BoreEvent& boreEvent : m_BoreEventManager.GetActiveEvents()){
            if(boreEvent.active){
                m_MaxBorePassedProgress =
                    std::max(m_MaxBorePassedProgress, boreEvent.progressMeters);
            }
        }
    }
    else{
        m_BoreAccumulator = 0.0f;
    }

    m_LastBoreDeltaTime = boreDeltaTime;

    UpdateWindowTitle();

    float profileDeltaTime = 0.0f;

    if(!m_ProfilePaused){
        profileDeltaTime = deltaTime;
    }

    if(m_ProfileMode == water::BoreProfileAnimationMode::OneShot){
        m_ProfileTime =
            std::min(
                m_ProfileTime + profileDeltaTime,
                m_BoreProfileConfig.duration
            );
    }
    else{
        m_ProfileTime += profileDeltaTime;
    }

    if(m_AutoRepeatEvent && m_BoreTime > m_EventRepeatDuration){
        m_BoreTime = 0.0f;
        m_BoreAccumulator = 0.0f;
        m_ProfileTime = 0.0f;
    }
}

void Stage12FluidFluxApp::PrepareFrame(uint32_t frameIndex, uint32_t imageIndex)
{
    UpdateCameraUniformBuffer(frameIndex);
    UpdateWaterParamsUniformBuffer(frameIndex);
    UpdateBoreFrontUniformBuffer(frameIndex);
    UpdateRiverFieldUniformBuffer(frameIndex);
    UpdateBoreProfileUniformBuffer(frameIndex);
    UpdateMultiBoreBuffers(frameIndex);
    UpdateFoamParamsUniformBuffer(frameIndex);
    UpdateFoamSimulationUniformBuffer(frameIndex);
    UpdateBoreWakeParamsUniformBuffer(frameIndex);
    UpdateWaterMaterialUniformBuffer(frameIndex);

    UpdateQuadtree(); // Tile 可见性依赖当前帧相机
    UpdateTileInstanceBuffer(frameIndex);
    UpdateCrestRibbonBuffer(frameIndex);
}

void Stage12FluidFluxApp::UpdateCameraUniformBuffer(uint32_t frameIndex)
{
    VkExtent2D extent = GetSwapChain().GetExtent();

    float aspect =
        static_cast<float>(extent.width) /
        static_cast<float>(extent.height);

    CameraUBO ubo{};
    ubo.model = glm::mat4(1.0f);
    ubo.view = m_Camera.GetViewMatrix();
    ubo.projection = m_Camera.GetProjectionMatrix(aspect);
    ubo.cameraWorldPosition =
        glm::vec4(
            m_Camera.GetPosition(),
            1.0f
        );
    ubo.debug = glm::ivec4(m_DebugMode, 0, 0, 0);

    m_CameraUniformBuffers[frameIndex]->CopyToMapped(
        &ubo,
        sizeof(ubo)
    );
}

void Stage12FluidFluxApp::UpdateBoreWakeParamsUniformBuffer(uint32_t frameIndex)
{
    water::BoreWakeParamsUBO ubo{};

    float resolution =
        static_cast<float>(
            glm::clamp(m_BoreWakeGui.resolution, 256, 2048)
        );

    ubo.domain =
        glm::vec4(
            m_RiverFieldConfig.worldMin.x,
            m_RiverFieldConfig.worldMin.y,
            m_RiverFieldConfig.worldSize,
            resolution
        );

    ubo.simulation =
        glm::vec4(
            m_LastSimulationDeltaTime,
            m_Time,
            m_BoreWakeGui.enabled ? 1.0f : 0.0f,
            m_BoreWakeGui.sourceStrength
        );

    ubo.range =
        glm::vec4(
            m_BoreWakeGui.wakeStart,
            m_BoreWakeGui.wakeEnd,
            m_BoreWakeGui.wakeFeather,
            m_BoreWakeGui.advectionSpeed
        );

    ubo.decay =
        glm::vec4(
            m_BoreWakeGui.aerationDecay,
            m_BoreWakeGui.foamDecay,
            m_BoreWakeGui.sedimentDecay,
            m_BoreWakeGui.turbulenceDecay
        );

    ubo.strength =
        glm::vec4(
            m_BoreWakeGui.aerationStrength,
            m_BoreWakeGui.foamStrength,
            m_BoreWakeGui.sedimentStrength,
            m_BoreWakeGui.turbulenceStrength
        );

    ubo.noise =
        glm::vec4(
            m_BoreWakeGui.patchThreshold,
            m_BoreWakeGui.warpStrength,
            m_BoreWakeGui.lateralFrequency,
            m_BoreWakeGui.backFrequency
        );

    m_BoreWakeParamsBuffers[frameIndex]->CopyToMapped(
        &ubo,
        sizeof(ubo)
    );
}

void Stage12FluidFluxApp::UpdateWaterParamsUniformBuffer(uint32_t frameIndex)
{
    water::WaterParamsUBO ubo{}; // GPU 端的 UBO 结构体
    ubo.patchLengths = glm::vec4(
        m_OceanConfig.spectrum.shortPatchLength,
        m_OceanConfig.spectrum.midPatchLength,
        m_OceanConfig.spectrum.longPatchLength,
        0.0f
    );// 每个级联的补丁边长（米），决定波浪波长和频域采样间距

    ubo.amplitudeScales = glm::vec4(
        m_OceanConfig.amplitudeScales[0],
        m_OceanConfig.amplitudeScales[1],
        m_OceanConfig.amplitudeScales[2],
        0.0f
    ); // 每个级联的振幅缩放因子，影响波浪高度

    ubo.metadata = glm::ivec4(
        static_cast<int>(water::kMaxFFTCascades),
        m_FFTEnabled ? 1 : 0,
        0,
        0
    );// 元数据，包括最大级联数

    ubo.simulation = glm::vec4(
        m_Time,
        1.0f,
        1.0f,
        static_cast<float>(m_DebugMode)
    ); // 时间、模拟参数和调试模式

    m_WaterParamsUniformBuffers[frameIndex]->CopyToMapped(
        &ubo,
        sizeof(ubo)
    );
}

void Stage12FluidFluxApp::UpdateBoreFrontUniformBuffer(uint32_t frameIndex)
{
    glm::vec2 direction =
        glm::normalize(m_BoreFrontParams.direction);

    water::BoreFrontUBO ubo{};

    // ubo.originSpeedTime：
        // .x, .y = 波前原点 origin（世界坐标）某一特定时刻，波锋线（Front Line）在推进方向上所处的几何位置
        // .z = 推进速度 speed（米/秒）
        // .w = 当前累计时间 m_BoreTime（秒），即 speed * time 中的 time，由 CPU 每帧累积
    ubo.originSpeedTime = glm::vec4(
        m_BoreFrontParams.origin,
        m_BoreFrontParams.speed,
        m_BoreTime
    );

    // ubo.directionLengthFade：
        // .x, .y = 波前推进方向 direction（归一化单位向量）
        // .z = 波前总长度 frontLength（米）
        // .w = 两端淡出比例 edgeFadeFraction（例如 0.03 表示两端各 3% 长度用于平滑消失）
    ubo.directionLengthFade = glm::vec4(
        direction,
        m_BoreFrontParams.frontLength,
        m_BoreFrontParams.edgeFadeFraction
    );

    // ubo.motionDebug：
        // .x = 初始偏移量 initialOffset（米）
        // .y = 波前剖面宽度 profileWidth（此处固定为 5.0 米）
        // .z = 浪尖掩码强度 ridgeStrength（调试模式启用时为 10.0，否则为 0）
        // .w = 是否启用涌潮效果 boreEnabled（1.0 启用，0.0 关闭）
    // Stage 8 Profile 接入后，默认关闭该 Ridge，避免重复叠加
    ubo.motionDebug = glm::vec4(
        m_BoreFrontParams.initialOffset,
        5.0f,
        m_BoreDebugRidgeEnabled ? 10.0f : 0.0f,
        m_BoreEnabled ? 1.0f : 0.0f
    );

    // ubo.lutInfo：
        // .x = Front LUT 的采样分辨率（如 1024）
        // .y = 分辨率的倒数（用于着色器中将采样点索引映射到 0~1 纹理坐标）
        // .z = 是否使用 LUT（1.0 启用弯曲波前，0.0 使用直线波前）
        // .w = 预留（目前为 0.0）
    ubo.lutInfo = glm::vec4(
        static_cast<float>(m_FrontLUT.resolution),
        1.0f / static_cast<float>(m_FrontLUT.resolution),
        m_BoreUseLUT ? 1.0f : 0.0f,
        0.0f
    );

    m_BoreFrontUniformBuffers[frameIndex]->CopyToMapped(
        &ubo,
        sizeof(ubo)
    );
}

void Stage12FluidFluxApp::UpdateRiverFieldUniformBuffer(
    uint32_t frameIndex
)
{
    // 更新河流场 UBO（每帧调用）
    // 将河流场的世界空间范围、河流总长度、涌潮波前的沿河进度和曲率参数写入当前帧的 Uniform Buffer
    water::RiverFieldUBO ubo{};

    // ===== domain：河流场纹理的世界空间范围与河流总长度 =====
    // x = worldMinX    – 河流场纹理左下角 X 坐标（米）
    // y = worldMinZ    – 河流场纹理左下角 Z 坐标（米）
    // z = worldSize    – 河流场纹理的边长（米）
    // w = riverLength  – 河流中轴线总长度（米）
    ubo.domain =
        glm::vec4(
            m_RiverFieldConfig.worldMin.x,  // worldMinX
            m_RiverFieldConfig.worldMin.y,  // worldMinZ
            m_RiverFieldConfig.worldSize,   // worldSize
            m_RiverLength  // 河流总长度（由 BakeRiverField 计算得出）
        );

    // ===== bore：涌潮波前的沿河进度与曲率控制 =====
    // x = boreProgressMeters   – 涌潮从入海口沿河流中轴线前进的总距离（米）
    //                           由 initialOffset + speed * time 计算，驱动潮头在河道中推进
    // y = riverBoreCurvatureMeters – 弯曲波前线的曲率影响范围（米），用于控制波前形变
    // z = amplitudeScale        – 河流涌潮的全局振幅缩放（1.0 = 标准强度）
    // w = decayRate             – 涌潮沿河道的振幅衰减速率（每米衰减比例）
    float boreProgressMeters =
        m_BoreFrontParams.initialOffset +
        m_BoreFrontParams.speed *
            m_BoreTime;

    ubo.bore =
        glm::vec4(
            boreProgressMeters,       // 波前沿河推进距离
            m_RiverBoreCurvatureMeters, // 弯曲波前的曲率影响范围
            1.0f,                     // 振幅缩放
            0.01f                     // 衰减速率
        );

    // 将 UBO 数据写入当前帧对应的 Host-Visible 缓冲区（持久映射，直接拷贝）
    m_RiverFieldUniformBuffers[frameIndex]
        ->CopyToMapped(
            &ubo,
            sizeof(ubo)
        );
}

void Stage12FluidFluxApp::UpdateBoreProfileUniformBuffer(uint32_t frameIndex)
{
    water::BoreProfileUBO ubo{};

    // ===== domain：涌潮剖面的空间与时域参数 =====
    // x = profileHalfWidth（米）：剖面覆盖的半宽度，控制涌潮波形沿距离轴的影响范围
    // y = waterRiseHeight（米）：潮后水位抬升高度，模拟涨潮时整体水位上升
    // z = riseWidth（米）：水位抬升的过渡宽度，控制从无到有的平滑过渡距离
    // w = duration（秒）：单次 OneShot 动画的总时长
    ubo.domain = glm::vec4(
        m_BoreProfileConfig.profileHalfWidth,
        m_BoreProfileGui.waterRiseHeight,
        m_BoreProfileGui.riseWidth,
        m_BoreProfileConfig.duration
    );

    // ===== animation：涌潮动画控制参数 =====
    // x = profileTime（秒）：当前动画时间，驱动 Wave Profile 的 V 轴采样，控制翻卷进度
    // y = profileWidth：剖面宽度缩放因子，在着色器中用于映射 signedDistance 到纹理 U 轴
    // z = animationMode：动画模式（0 = OneShot 单次播放，1 = Looping 循环播放）
    // w = profileEnabled：剖面效果开关（1.0 = 启用涌潮，0.0 = 关闭）
    float boreProgress =
        m_BoreFrontParams.initialOffset +
        m_BoreFrontParams.speed *
        m_BoreTime;

    float travelPhase =
        glm::clamp(
            boreProgress / glm::max(m_RiverLength, 1.0f),
            0.0f,
            1.0f
        );

    float profilePhase = m_BoreProfileGui.fixedPhase;

    if(!m_ProfilePaused){
        if(travelPhase < 0.15f){
            float t =
                glm::clamp(
                    travelPhase / 0.15f,
                    0.0f,
                    1.0f
                );

            profilePhase =
                glm::mix(
                    0.0f,
                    0.60f,
                    t
                );
        }
        else if(travelPhase < 0.80f){
            profilePhase = 0.60f;
        }
        else{
            float t =
                glm::clamp(
                    (travelPhase - 0.80f) / 0.20f,
                    0.0f,
                    1.0f
                );

            profilePhase =
                glm::mix(
                    0.60f,
                    1.0f,
                    t
                );
        }
    }

    float profileTime =
        profilePhase *
        m_BoreProfileConfig.duration;
    
    ubo.animation = glm::vec4(
        profileTime,
        m_BoreProfileGui.profileWidthScale,
        static_cast<float>(static_cast<int>(m_ProfileMode)),
        m_ProfileEnabled ? 1.0f : 0.0f
    );

    // ===== geometry：几何变换缩放因子 =====
    // x = globalAmplitude：全局振幅缩放，整体调节涌潮波高
    // y = forwardScale：前向水平位移缩放，控制推挤强度
    // z = upwardScale：向上垂直位移缩放，控制浪高
    // w = activeRegionMask：当前点是否在涌潮活跃区域内（由 Flow Map / SDF 控制）
    ubo.geometry = glm::vec4(
        m_BoreProfileGui.globalAmplitude,
        m_BoreProfileGui.forwardScale,
        m_BoreProfileGui.upwardScale,
        m_BoreProfileGui.activeRegionMask
    );

    // ===== suppression：FFT 背景波浪抑制系数（潮头浪尖处压低 FFT 波浪） =====
    // x = shortWaveSuppression：短波（高频）抑制系数，推荐 0.2~0.4
    // y = midWaveSuppression：中波抑制系数，推荐 0.35~0.7
    // z = longWaveSuppression：长波（低频）抑制系数，推荐 0.7~1.0
    // w = 预留（未使用）
    // 原则：短波在潮头处被压得最狠，长涌浪保留最多，这样更自然
    ubo.suppression = m_BoreProfileGui.suppression;

    m_BoreProfileUniformBuffers[frameIndex]->CopyToMapped(
        &ubo,
        sizeof(ubo)
    );
}

void Stage12FluidFluxApp::UpdateMultiBoreBuffers(uint32_t frameIndex)
{
    const std::vector<water::BoreEvent>& events =
        m_BoreEventManager.GetActiveEvents();

    m_BoreEventGpuScratch.fill(water::BoreEventGPU{});

    uint32_t activeCount = 0;

    for(const water::BoreEvent& event : events){
        if(!event.active || activeCount >= water::kMaxBoreEvents){
            continue;
        }

        water::BoreEventGPU gpu{};
        gpu.motion = glm::vec4(
            event.progressMeters,
            event.speed,
            event.age,
            1.0f
        );

        gpu.shape = glm::vec4(
            event.amplitudeScale,
            event.widthScale,
            event.forwardScale,
            event.curvatureScale
        );

        gpu.appearance = glm::vec4(
            event.foamScale,
            ComputeProfilePhaseForProgress(
                event.progressMeters,
                event.profilePhaseOffset
            ),
            event.variationPhase,
            static_cast<float>(event.seed) / 4294967295.0f
        );

        gpu.suppression = m_BoreProfileGui.suppression;

        m_BoreEventGpuScratch[activeCount] = gpu;
        ++activeCount;
    }

    // 单潮头路径已移除，恒定启用多潮头。
    water::MultiBoreUBO ubo{};
    ubo.metadata = glm::ivec4(
        static_cast<int>(activeCount),
        static_cast<int>(water::kMaxBoreEvents),
        1,                            // z = 恒定启用多潮头
        m_BoreFieldMode == BoreFieldMode::ProgressField ? 1 : 0   // ← 新
    );

    ubo.river = glm::vec4(
        m_RiverFieldConfig.worldMin.x,
        m_RiverFieldConfig.worldMin.y,
        m_RiverFieldConfig.worldSize,
        m_RiverLength
    );

    ubo.crestNoiseA = glm::vec4(
        m_CrestNoiseGui.lateralFrequency,
        m_CrestNoiseGui.alongFrequencyX,
        m_CrestNoiseGui.alongFrequencyY,
        m_CrestNoiseGui.animationSpeed
    );
    ubo.crestNoiseB = glm::vec4(
        m_CrestNoiseGui.detailFrequency,
        m_CrestNoiseGui.detailWeight,
        m_CrestNoiseGui.amplitudeMin,
        m_CrestNoiseGui.amplitudeMax
    );
    ubo.crestNoiseC = glm::vec4(
        m_CrestNoiseGui.wobbleStrength,
        m_CrestNoiseGui.wobbleFrequency,
        m_RiverBoreCurvatureMeters,
        0.0f
    );

    ubo.curve = glm::vec4(
        m_CrestRibbonGui.curveMeters,
        m_CrestRibbonGui.irregularCurveMeters,
        m_CrestRibbonGui.curveFrequency,
        0.0f
    );

    ubo.persistent = glm::vec4(
        m_MaxBorePassedProgress,
        m_MultiBoreGui.lateralExtent,
        m_MultiBoreGui.lateralFade,
        0.0f);

    m_MultiBoreUniformBuffers[frameIndex]->CopyToMapped(
        &ubo,
        sizeof(ubo)
    );

    m_BoreEventBuffers[frameIndex]->CopyToMapped(
        m_BoreEventGpuScratch.data(),
        sizeof(water::BoreEventGPU) * water::kMaxBoreEvents
    );
}

water::BoreEventManagerConfig Stage12FluidFluxApp::BuildBoreEventManagerConfig() const
{
    water::BoreEventManagerConfig config{};
    config.enabled = m_MultiBoreGui.enabled;
    config.minSpawnInterval = m_MultiBoreGui.minSpawnInterval;
    config.maxSpawnInterval = m_MultiBoreGui.maxSpawnInterval;
    config.retryMinInterval = m_MultiBoreGui.retryMinInterval;
    config.retryMaxInterval = m_MultiBoreGui.retryMaxInterval;
    config.baseSpeed = m_MultiBoreGui.baseSpeed;
    config.removeMargin = m_MultiBoreGui.removeMargin;
    config.minimumSeparationPadding = m_MultiBoreGui.minimumSeparationPadding;
    return config;
}

float Stage12FluidFluxApp::ComputeProfilePhaseForProgress(
    float progressMeters,
    float phaseOffset
) const
{
    float travelPhase =
        glm::clamp(
            progressMeters / glm::max(m_RiverLength, 1.0f),
            0.0f,
            1.0f
        );

    float profilePhase = m_BoreProfileGui.fixedPhase;

    if(!m_ProfilePaused){
        if(travelPhase < 0.15f){
            float t = glm::clamp(travelPhase / 0.15f, 0.0f, 1.0f);
            profilePhase = glm::mix(0.0f, 0.60f, t);
        }
        else if(travelPhase < 0.80f){
            profilePhase = 0.60f;
        }
        else{
            float t = glm::clamp((travelPhase - 0.80f) / 0.20f, 0.0f, 1.0f);
            profilePhase = glm::mix(0.60f, 1.0f, t);
        }
    }

    return glm::clamp(profilePhase + phaseOffset * 0.08f, 0.0f, 1.0f);
}

void Stage12FluidFluxApp::ResetMultiBoreEvents()
{
    m_BoreEventManager.Reset(static_cast<uint32_t>(m_MultiBoreGui.seed));
    m_MaxBorePassedProgress = -1.0e9f;
}

void Stage12FluidFluxApp::UpdateFoamParamsUniformBuffer(uint32_t frameIndex)
{
    water::FoamParamsUBO ubo{};

    // ==== animation：泡沫动画控制参数 =====
    ubo.animation = glm::vec4(
        m_Time,
        m_FoamGui.animationCycle,
        m_FoamGui.detailWorldScale,
        1.0f
    );

    // ==== sourceStrength：泡沫强度控制 =====
    // 想让潮头更白：调大 x/w。
    // 想让海浪自身白沫更多：调大 y/z
    ubo.sourceStrength = m_FoamGui.sourceStrength;

    // ==== thresholds：泡沫生成阈值控制 =====
    // x 越低，越容易出泡沫；y 越低，泡沫更快变满
    ubo.thresholds = m_FoamGui.thresholds;

    // ==== appearance：泡沫外观控制 =====
    ubo.appearance = m_FoamGui.appearance;

    ubo.state = glm::vec4(
        m_FoamGui.stateGain,
        m_FoamGui.stateDecay,
        m_FoamGui.stateDiffusion,
        m_FoamGui.stateEnabled
    );

    uint32_t outputFoamStateIndex =
        1 - m_CurrentFoamStateIndex;

    ubo.runtime = glm::vec4(
        static_cast<float>(outputFoamStateIndex),
        1.0f,
        m_FoamGui.oceanFoamFadeNear,
        m_FoamGui.oceanFoamFadeFar
    );

    ubo.domain = m_FoamGui.domain;

    ubo.foamShallow = glm::vec4(
        m_FoamGui.foamShallowOffset,
        m_FoamGui.foamShallowScale,
        m_FoamGui.foamHardnessIntensity,
        m_FoamGui.foamHardnessWidth);

    ubo.foamSoft = glm::vec4(
        m_FoamGui.foamSoftVelocity,
        m_FoamGui.foamSoftBase,
        m_FoamGui.foamSoftMax,
        m_FoamGui.foamAlpha);

    ubo.boreWake0 =
        glm::vec4(
            static_cast<float>(m_CurrentBoreWakeStateIndex),
            m_BoreWakeGui.enabled ? 1.0f : 0.0f,
            m_BoreWakeGui.foamStrength,
            m_BoreWakeGui.aerationStrength
        );

    ubo.boreWake1 =
        glm::vec4(
            m_BoreWakeGui.sedimentStrength,
            m_BoreWakeGui.turbulenceStrength,
            m_MultiBoreGui.lateralExtent,
            m_MultiBoreGui.lateralFade
        );

    m_FoamParamsUniformBuffers[frameIndex]->CopyToMapped(
        &ubo,
        sizeof(ubo)
    );
}

void Stage12FluidFluxApp::UpdateFoamSimulationUniformBuffer(uint32_t frameIndex)
{
    // 限制时间步长，防止泡沫平流和扩散不稳定（最大 1/30 秒）
    float foamDt =
        std::min(
            m_LastSimulationDeltaTime,
            1.0f / 30.0f
        );

    m_LastFoamDeltaTime = foamDt;

    water::FoamSimulationUBO ubo{};

    // ===== domain：泡沫状态图的物理范围 =====
    // xy: 世界空间左下角坐标（-128, -128）
    // zw: 世界空间宽度和高度（256, 256）
    // 泡沫状态图覆盖 256m × 256m 的区域，中心在原点
    ubo.domain = m_FoamGui.domain;

    // ===== simulation：时变模拟参数 =====
    // x: foamDt    – 当前帧的时间步长（已限制）
    // y: m_Time    – 当前累积时间
    // z: 1.8f      – 源项增益（与 FoamParamsUBO.state.x 保持一致）
    // w: 0.45f     – 衰减系数（与 FoamParamsUBO.state.y 保持一致）
    ubo.simulation = glm::vec4(
        foamDt,
        m_Time,
        m_FoamGui.stateGain,
        m_FoamGui.stateDecay
    );

    // ===== solver：数值求解参数 =====
    // x: 0.02f – 扩散系数（拉普拉斯项的权重）
    // y: m_FoamResolution      – 泡沫状态图的分辨率（如 512）
    // z: 1.0 / m_FoamResolution – 单个纹素的大小（逆分辨率），用于计算相邻像素 UV 偏移
    // w: 1.0f  – 求解器开关（1 启用，0 跳过）
    ubo.solver = glm::vec4(
        m_FoamGui.stateDiffusion,
        static_cast<float>(m_FoamResolution),
        1.0f / static_cast<float>(m_FoamResolution),
        m_FoamGui.solverEnabled ? 1.0f : 0.0f
    );

    // 将数据写入当前帧对应的 Uniform Buffer（持久映射，直接拷贝）
    m_FoamSimulationUniformBuffers[frameIndex]->CopyToMapped(
        &ubo,
        sizeof(ubo)
    );
}

// 更新水体材质 UBO（每帧、每飞行帧调用）
// 将预设的水体颜色、光学参数、光照方向和雾效参数写入当前帧的 Uniform Buffer
void Stage12FluidFluxApp::UpdateWaterMaterialUniformBuffer(uint32_t frameIndex)
{
    water::WaterMaterialUBO ubo{};

    // ===== 水体分层颜色 =====
    // 浅水颜色：偏亮的青绿色，模拟近岸/浅滩的水色
    ubo.shallowColor = m_WaterMaterialGui.shallowColor;
    // 深水颜色：暗蓝黑色，模拟远海/深水的深邃感
    ubo.deepColor = m_WaterMaterialGui.deepColor;
    // 泥沙颜色：棕黄色，用于模拟浑浊水体（如河口、风暴后）
    ubo.sedimentColor = m_WaterMaterialGui.sedimentColor;

    // ===== 光学参数 =====
    // x: F0 = 0.0204f – 基础反射率，控制水面的基底反射强度
    // y: reflectionStrength = 0.35   – 天空反射强度
    // z: roughness = 0.45            – 粗糙度，影响折射强度
    // w: sedimentAmount = 0.35       – 泥沙混合量，值越大浅水越浑浊
    ubo.opticalParams = m_WaterMaterialGui.opticalParams;

    // ===== 光照参数 =====
    // xyz: sunDirection = (-0.35, 0.85, 0.25) 归一化 – 太阳方向（斜上方偏左）
    // w: specularStrength = 1.2                 – 太阳高光强度
    ubo.lightParams = glm::vec4(
        glm::normalize(m_WaterMaterialGui.sunDirection),
        m_WaterMaterialGui.specularStrength
    );

    // ===== 雾与远景参数 =====
    // x: fogStart = 120.0    – 雾开始距离（米），120米内无雾
    // y: fogEnd = 700.0      – 雾完全覆盖距离（米），700米外完全被雾遮挡
    // z: horizonFade = 0.4   – 地平线融合强度，柔和过渡远处水面与天空
    // w: 0.0                 – 预留
    ubo.fogParams = m_WaterMaterialGui.fogParams;

    // ===== 吸收参数 =====
    // 吸收参数：RGB 每米吸收系数 + 填充 0
    ubo.absorptionCoeff = glm::vec4(m_WaterMaterialGui.absorption, 0.0f);
    // 浅水参数：x=河床反照率, y=最大可见水深, z=每米不透明度斜率, w=俯视光程加成
    ubo.shallowParams = glm::vec4(m_WaterMaterialGui.bedAlbedo, 
                              m_WaterMaterialGui.maxVisibleDepth, 
                              m_WaterMaterialGui.shallowBlend,
                              m_WaterMaterialGui.depthUpwardBlend);
    
    // ===== FF MF_WaterTransition / MF_SingleLayerWater =====
    ubo.absorptionShore  = glm::vec4(m_WaterMaterialGui.absorptionShore, 0.0f);
    ubo.scatteringDeep   = glm::vec4(m_WaterMaterialGui.scatteringDeep,
                                     m_WaterMaterialGui.scatterGain);
    ubo.scatteringShore  = glm::vec4(m_WaterMaterialGui.scatteringShore,
                                     m_WaterMaterialGui.foamScatterScale);

    // FF: PhaseG = lerp(_PhaseGDeepSunLow, _PhaseGDeepSunHigh, saturate(sunDir.y))
    glm::vec3 sunDirN = glm::normalize(m_WaterMaterialGui.sunDirection);
    float phaseG = glm::mix(-0.7f, 0.5f, glm::clamp(sunDirN.y, 0.0f, 1.0f));

    ubo.shoreBlend = glm::vec4(m_WaterMaterialGui.shoreDepthNorm,
                               m_WaterMaterialGui.shoreDistNorm,
                               phaseG,
                               m_WaterMaterialGui.waterLevel);
    ubo.colorBehind = glm::vec4(m_WaterMaterialGui.colorBehind, 1.0f);

    // ===== FF MF_FluidWaterLayer：高光 / 粗糙度 =====
    ubo.waterSpecular = glm::vec4(
        m_WaterMaterialGui.specBias,
        m_WaterMaterialGui.specScale,
        m_WaterMaterialGui.specPower,
        m_WaterMaterialGui.specHorizonFloor);
    ubo.specularHorizon = glm::vec4(
        m_WaterMaterialGui.specHorizonDistance,
        m_WaterMaterialGui.specHorizonOffset,
        m_WaterMaterialGui.roughFromFresnel,
        m_WaterMaterialGui.roughMin);
    ubo.cheapScatter = glm::vec4(
        m_WaterMaterialGui.scatterDetails,
        m_WaterMaterialGui.scatterPower,
        m_WaterMaterialGui.scatterScale,
        m_WaterMaterialGui.normalFixStrength);
    
    // 将数据写入当前帧对应的 Uniform Buffer（持久映射，直接拷贝）
    m_WaterMaterialUniformBuffers[frameIndex]->CopyToMapped(
        &ubo,
        sizeof(ubo)
    );
}

// 判断 Tile 是水域、陆地还是河岸
bool Stage12FluidFluxApp::ClassifyRiverTile(
    water::WaterTile& tile
) const
{
    if(!m_RiverField){
        tile.intersectsWater = true;
        tile.intersectsBank = false;
        return true;
    }

    if(tile.key.level < 4){
        tile.intersectsWater = true;
        tile.intersectsBank = false;
        return true;
    }

    glm::vec2 minPoint =
        tile.worldMin;

    glm::vec2 maxPoint =
        tile.worldMin +
        glm::vec2(tile.worldSize);

    float classifyMargin =
        std::max(
            m_RiverFieldConfig.worldSize /
                static_cast<float>(m_RiverFieldConfig.resolution),
            tile.worldSize /
                static_cast<float>(m_QuadtreeGui.patchCellCount)
        );

    minPoint -= glm::vec2(classifyMargin);
    maxPoint += glm::vec2(classifyMargin);

    glm::vec2 center =
        (minPoint + maxPoint) * 0.5f;

    // 粗 tile 覆盖上千米，9 点估 progress 范围会漏掉潮头 → 相邻 tile LOD 差 5 级出现立方体阶梯。
    // 采样密度随 tile 尺寸自适应：保证采样间距不超过约 64m。
    const int sampleDim = glm::clamp(
        static_cast<int>(tile.worldSize / 64.0f) + 1, 3, 17);

    std::vector<glm::vec2> samplePoints;
    samplePoints.reserve(static_cast<size_t>(sampleDim) * sampleDim);
    for(int j = 0; j < sampleDim; ++j){
        for(int i = 0; i < sampleDim; ++i){
            float fx = static_cast<float>(i) / static_cast<float>(sampleDim - 1);
            float fz = static_cast<float>(j) / static_cast<float>(sampleDim - 1);
            samplePoints.push_back(
                minPoint + glm::vec2(fx, fz) * (maxPoint - minPoint));
        }
    }

    bool anyWater = false;
    bool anyDry = false;
    bool nearBank = false;

    tile.minRiverProgress =
        std::numeric_limits<float>::max();

    tile.maxRiverProgress =
        -std::numeric_limits<float>::max();

    for(const glm::vec2& point : samplePoints){
        glm::vec4 flow =
            m_RiverField->SampleFlowNearest(point);

        glm::vec4 coord =
            m_RiverField->SampleCoordinateNearest(point);

        if(flow.a > 0.01f){
            anyWater = true;
        }
        else{
            anyDry = true;
        }

        if(flow.a > 0.01f){
            float progressMeters =
                coord.r *
                m_RiverLength;

            tile.minRiverProgress =
                std::min(
                    tile.minRiverProgress,
                    progressMeters
                );

            tile.maxRiverProgress =
                std::max(
                    tile.maxRiverProgress,
                    progressMeters
                );
        }

        float bankDistance =
            coord.b * 16.0f;

        if(std::abs(bankDistance) < 24.0f){
            nearBank = true;
        }
    }

    tile.intersectsWater = anyWater;
    tile.intersectsBank = anyWater && (anyDry || nearBank);

    if(tile.minRiverProgress >
        tile.maxRiverProgress)
        {
            tile.minRiverProgress = 0.0f;
            tile.maxRiverProgress = 0.0f;
        }

    return tile.intersectsWater;
}

water::RiverSamplePoint Stage12FluidFluxApp::SampleRiverAtProgress(
    float progressMeters
) const
{
    const std::vector<water::RiverSamplePoint>& samples =
        m_RiverSpline.GetSamples();

    if(samples.empty()){
        water::RiverSamplePoint fallback{};
        fallback.position = glm::vec2(0.0f);
        fallback.tangent = glm::vec2(1.0f, 0.0f);
        fallback.progressMeters = 0.0f;
        fallback.halfWidth = 100.0f;
        fallback.boreAmplitude = 1.0f;
        fallback.curvatureWeight = 0.0f;
        return fallback;
    }

    progressMeters =
        glm::clamp(
            progressMeters,
            samples.front().progressMeters,
            samples.back().progressMeters
        );

    auto it = std::lower_bound(
        samples.begin(),
        samples.end(),
        progressMeters,
        [](const water::RiverSamplePoint& sample, float value){
            return sample.progressMeters < value;
        }
    );

    if(it == samples.begin()){
        return samples.front();
    }

    if(it == samples.end()){
        return samples.back();
    }

    const water::RiverSamplePoint& b = *it;
    const water::RiverSamplePoint& a = *(it - 1);

    float denom =
        glm::max(b.progressMeters - a.progressMeters, 0.001f);

    float t =
        glm::clamp(
            (progressMeters - a.progressMeters) / denom,
            0.0f,
            1.0f
        );

    water::RiverSamplePoint result{};
    result.position = glm::mix(a.position, b.position, t);
    result.tangent = glm::normalize(glm::mix(a.tangent, b.tangent, t));
    result.progressMeters = progressMeters;
    result.halfWidth = glm::mix(a.halfWidth, b.halfWidth, t);
    result.boreAmplitude = glm::mix(a.boreAmplitude, b.boreAmplitude, t);
    result.curvatureWeight = glm::mix(a.curvatureWeight, b.curvatureWeight, t);
    return result;
}

// 强制河岸区域使用高 LOD, 为河岸 Tile 指定最低 LOD 层级
    // coarse tile 先至少分到 Level 4
    // 岸边至少 Level 5
    // 潮头附近强制 Level 6
uint32_t Stage12FluidFluxApp::GetRiverRequiredLevel(
    const water::WaterTile& tile
) const
{
    uint32_t requiredLevel = 0;

    if(tile.key.level < 4){
        requiredLevel =
            std::max(requiredLevel, 4u);
    }

    if(tile.intersectsBank){
        requiredLevel =
            std::max(requiredLevel, 4u);
    }

    // 不再把 500m 尾迹全拉到 maxLevel。核心仅 80m level 6，附近 220m level 5
    bool hasProgressRange =
        tile.maxRiverProgress >
        tile.minRiverProgress;

    if(hasProgressRange){
        const std::vector<water::BoreEvent>& events =
            m_BoreEventManager.GetActiveEvents();

        float nearestBoreDist = 1.0e9f;

        for(const water::BoreEvent& event : events){
            if(!event.active){
                continue;
            }

            float bp = event.progressMeters;

            float d =
                std::max(
                    0.0f,
                    std::max(
                        tile.minRiverProgress - bp,
                        bp - tile.maxRiverProgress
                    )
                );

            nearestBoreDist =
                std::min(nearestBoreDist, d);
        }

        uint32_t coreLevel =
            static_cast<uint32_t>(
                glm::clamp(
                    m_QuadtreeGui.boreCoreLevel,
                    0,
                    m_QuadtreeGui.maxLevel
                )
            );

        uint32_t nearLevel =
            static_cast<uint32_t>(
                glm::clamp(
                    m_QuadtreeGui.boreNearLevel,
                    0,
                    m_QuadtreeGui.maxLevel
                )
            );

        uint32_t ultraLevel =
            static_cast<uint32_t>(
                glm::clamp(
                    m_QuadtreeGui.boreUltraLevel,
                    0,
                    m_QuadtreeGui.maxLevel
                )
            );

        if(nearestBoreDist <= m_QuadtreeGui.boreUltraWidth){
            requiredLevel = std::max(requiredLevel, ultraLevel);
        }
        else if(nearestBoreDist <= m_QuadtreeGui.boreCoreWidth){
            requiredLevel = std::max(requiredLevel, coreLevel);
        }
        else if(nearestBoreDist <= m_QuadtreeGui.boreNearWidth){
            requiredLevel = std::max(requiredLevel, nearLevel);
        }
    }

    return requiredLevel;
}

// 每帧根据相机生成可见 Tile
void Stage12FluidFluxApp::UpdateQuadtree()
{
    if(!m_WaterQuadtree){
        return;
    }

    VkExtent2D extent =
        GetSwapChain().GetExtent();

    float aspect =
        static_cast<float>(extent.width) /
        static_cast<float>(extent.height);

    glm::mat4 view =
        m_Camera.GetViewMatrix();

    glm::mat4 projection =
        m_Camera.GetProjectionMatrix(aspect);

    glm::mat4 viewProjection =
        projection * view;

    m_WaterQuadtree->Build(
        m_Camera.GetPosition(),
        viewProjection,
        extent.height
    );

    m_VisibleWaterTiles =
        m_WaterQuadtree->GetVisibleTiles();

    // 水体写深度后，绘制顺序决定每像素被混合几次。
    // 近→远排序：近处片元先写入深度，被浪墙遮挡的远处水面直接深度测试失败，
    // 保证每像素只混合一次，避免重叠处颜色被叠暗。
    const glm::vec2 cameraXZ(
        m_Camera.GetPosition().x,
        m_Camera.GetPosition().z);

    std::sort(
        m_VisibleWaterTiles.begin(),
        m_VisibleWaterTiles.end(),
        [cameraXZ](const water::WaterTile& a, const water::WaterTile& b){
            glm::vec2 da =
                a.worldMin + glm::vec2(a.worldSize * 0.5f) - cameraXZ;
            glm::vec2 db =
                b.worldMin + glm::vec2(b.worldSize * 0.5f) - cameraXZ;
            return glm::dot(da, da) < glm::dot(db, db);
        }
    );
}

void Stage12FluidFluxApp::UpdateTileInstanceBuffer(
    uint32_t frameIndex
)
{
    if(m_VisibleWaterTiles.size() > m_MaxVisibleWaterTiles){
        VKP_WARN(
            "Visible water tiles overflow: {} > {}",
            m_VisibleWaterTiles.size(),
            m_MaxVisibleWaterTiles
        );
    }

    m_CurrentVisibleWaterTileCount =
        static_cast<uint32_t>(
            std::min<size_t>(
                m_VisibleWaterTiles.size(),
                m_MaxVisibleWaterTiles
            )
        );

    std::vector<water::WaterTileGPU> gpuTiles;
    gpuTiles.resize(m_CurrentVisibleWaterTileCount);

    for(uint32_t i = 0; i < m_CurrentVisibleWaterTileCount; ++i){
        const water::WaterTile& tile =
            m_VisibleWaterTiles[i];

        water::WaterTileGPU gpuTile{};
        gpuTile.originSize =
            glm::vec4(
                tile.worldMin.x,
                tile.worldMin.y,
                tile.worldSize,
                tile.morphAlpha
            );

        gpuTile.metadata =
            glm::uvec4(
                tile.key.level,
                tile.edgeMask,
                0u,
                0u
            );

        gpuTiles[i] = gpuTile;
    }

    if(m_CurrentVisibleWaterTileCount == 0){
        return;
    }

    VkDeviceSize copySize =
        sizeof(water::WaterTileGPU) *
        m_CurrentVisibleWaterTileCount;

    m_TileInstanceBuffers[frameIndex]->CopyToMapped(
        gpuTiles.data(),
        copySize
    );
}

void Stage12FluidFluxApp::UpdateCrestRibbonBuffer(uint32_t frameIndex)
{
    m_CrestRibbonVertexCount = 0;

    if(!m_CrestRibbonGui.enabled || !m_CrestRibbonVertexBuffers[frameIndex]){
        return;
    }

    int lateralSegments =
        glm::clamp(m_CrestRibbonGui.lateralSegments, 16, 512);

    int depthSegments =
        glm::clamp(m_CrestRibbonGui.depthSegments, 2, 8);

    std::vector<CrestRibbonVertex> vertices;
    vertices.reserve(
        static_cast<size_t>(water::kMaxBoreEvents) *
        lateralSegments *
        depthSegments *
        6
    );

    auto makeVertex =
        [&](const water::BoreEvent& event,
            float lateralNorm,
            float depth01) -> CrestRibbonVertex
        {
            // Ribbon 只负责潮脊。宽白水交给 Wake State
            float localS =
                glm::mix(
                    -m_CrestRibbonGui.wakeWidth,
                    m_CrestRibbonGui.frontWidth,
                    depth01
                );

            float normalizedLateral =
                glm::clamp(lateralNorm, -1.0f, 1.0f);

            // 潮线变成宏观弯曲 + 低频随机曲线，不再直线
            float eventSeed =
                static_cast<float>(event.seed) / 4294967295.0f;

            float x =
                normalizedLateral /
                std::max(m_MultiBoreGui.lateralExtent, 0.001f);

            water::RiverSamplePoint baseSample =
                SampleRiverAtProgress(event.progressMeters + localS);

            float baseCurve =
                m_CrestRibbonGui.curveMeters *
                (x * x - 0.20f);

            float irregularCurve =
                m_CrestRibbonGui.irregularCurveMeters *
                (
                    0.70f *
                    std::sin(glm::pi<float>() * x * m_CrestRibbonGui.curveFrequency +
                             eventSeed * glm::two_pi<float>()) +
                    0.30f *
                    std::sin(glm::two_pi<float>() * x +
                             eventSeed * 11.7f)
                );

            float curveOffset =
                baseSample.curvatureWeight *
                event.curvatureScale *
                (baseCurve + irregularCurve);

            float progress =
                event.progressMeters +
                localS +
                curveOffset;

            water::RiverSamplePoint sample =
                SampleRiverAtProgress(progress);

            glm::vec2 side =
                glm::normalize(glm::vec2(
                    -sample.tangent.y,
                     sample.tangent.x
                ));

            float halfWidth =
                sample.halfWidth *
                m_MultiBoreGui.lateralExtent;

            glm::vec2 worldXZ =
                sample.position +
                side * normalizedLateral * halfWidth;

            float edgeAlpha =
                1.0f -
                glm::smoothstep(
                    m_MultiBoreGui.lateralExtent - m_CrestRibbonGui.edgeFade,
                    m_MultiBoreGui.lateralExtent,
                    std::abs(normalizedLateral)
                );

            float crestShape =
                std::exp(
                    -(localS * localS) /
                    glm::max(
                        2.0f *
                        m_BoreProfileConfig.crestWidth *
                        m_BoreProfileConfig.crestWidth,
                        0.001f
                    )
                );

            float sigma =
                std::max(m_CrestRibbonGui.hardCrestWidth, 0.001f);

            float crestCore =
                std::exp(
                    -0.5f *
                    localS * localS /
                    (sigma * sigma)
                );

            float alpha =
                m_CrestRibbonGui.alpha *
                edgeAlpha *
                glm::clamp(crestCore, 0.0f, 1.0f);

            // ribbon 永远在潮头水面上方；潮脊高度有缓慢明显变化
            float heightNoise =
                0.65f *
                std::sin(glm::two_pi<float>() * (x * 1.35f + eventSeed)) +
                0.35f *
                std::sin(glm::two_pi<float>() * (x * 2.70f + eventSeed * 3.1f));


            // 历史水位抬升：只要这个位置在历史最远潮头后方，就把 ribbon 放到抬升后的水面上方。
            // 否则第二、第三个潮头会被已抬高的水面 depth 挡住。
            float passedDistance =
                progress - m_MaxBorePassedProgress;

            float persistentWidth =
                std::max(m_BoreProfileGui.riseWidth, m_BoreProfileConfig.profileHalfWidth) * 2.0f;

            float persistentRise =
                (1.0f - glm::smoothstep(-persistentWidth, 0.0f, passedDistance)) *
                m_BoreProfileGui.waterRiseHeight *
                sample.boreAmplitude;

            float eventHeight =
                m_BoreProfileConfig.crestHeight *
                m_BoreProfileGui.upwardScale *
                m_BoreProfileGui.globalAmplitude *
                sample.boreAmplitude *
                event.amplitudeScale;

            float wakeLift = 0.0f;

            float y =
                m_WaterMaterialGui.waterLevel +
                persistentRise +
                m_CrestRibbonGui.heightOffset +
                wakeLift +
                (
                    m_CrestRibbonGui.crestHeightOffset +
                    eventHeight * 1.05f +
                    m_CrestRibbonGui.heightVariation * heightNoise
                ) * crestShape;

            CrestRibbonVertex vertex{};
            vertex.positionAlpha =
                glm::vec4(worldXZ.x, y, worldXZ.y, alpha);
            vertex.param =
                glm::vec4(
                    normalizedLateral,
                    depth01,
                    localS,
                    static_cast<float>(event.seed) / 4294967295.0f
                );

            vertex.param2 =
                glm::vec4(
                    m_CrestRibbonGui.edgeJitterMeters,
                    m_CrestRibbonGui.wakePatchThreshold,
                    m_CrestRibbonGui.wakeFoamStrength,
                    m_CrestRibbonGui.wakeHoleStrength
                );

            vertex.param3 =
                glm::vec4(
                    m_Time,
                    m_CrestRibbonGui.wakeWidth,
                    m_CrestRibbonGui.frontWidth,
                    0.0f
                );

            vertex.param4 =
                glm::vec4(
                    m_CrestRibbonGui.hardCrestWidth,
                    m_CrestRibbonGui.wakeStart,
                    m_CrestRibbonGui.wakeEnd,
                    m_CrestRibbonGui.wakeFeather
                );

            return vertex;
        };

    const std::vector<water::BoreEvent>& events =
        m_BoreEventManager.GetActiveEvents();

    for(const water::BoreEvent& event : events){
        if(!event.active){
            continue;
        }

        for(int z = 0; z < depthSegments; ++z){
            float v0 = static_cast<float>(z) /
                static_cast<float>(depthSegments);
            float v1 = static_cast<float>(z + 1) /
                static_cast<float>(depthSegments);

            for(int x = 0; x < lateralSegments; ++x){
                float u0 =
                    glm::mix(
                        -m_MultiBoreGui.lateralExtent,
                         m_MultiBoreGui.lateralExtent,
                        static_cast<float>(x) /
                            static_cast<float>(lateralSegments)
                    );

                float u1 =
                    glm::mix(
                        -m_MultiBoreGui.lateralExtent,
                         m_MultiBoreGui.lateralExtent,
                        static_cast<float>(x + 1) /
                            static_cast<float>(lateralSegments)
                    );

                CrestRibbonVertex a = makeVertex(event, u0, v0);
                CrestRibbonVertex b = makeVertex(event, u1, v0);
                CrestRibbonVertex c = makeVertex(event, u1, v1);
                CrestRibbonVertex d = makeVertex(event, u0, v1);

                vertices.push_back(a);
                vertices.push_back(b);
                vertices.push_back(c);

                vertices.push_back(a);
                vertices.push_back(c);
                vertices.push_back(d);
            }
        }
    }

    m_CrestRibbonVertexCount =
        static_cast<uint32_t>(
            std::min<size_t>(
                vertices.size(),
                m_CrestRibbonVertexCapacity
            )
        );

    if(m_CrestRibbonVertexCount == 0){
        return;
    }

    m_CrestRibbonVertexBuffers[frameIndex]->CopyToMapped(
        vertices.data(),
        sizeof(CrestRibbonVertex) * m_CrestRibbonVertexCount
    );
}

void Stage12FluidFluxApp::DrawCrestRibbon(
    VkCommandBuffer commandBuffer,
    uint32_t currentFrame
)
{
    if(!m_CrestRibbonPipeline ||
       m_CrestRibbonVertexCount == 0 ||
       !m_CrestRibbonVertexBuffers[currentFrame]){
        return;
    }

    vkCmdBindPipeline(
        commandBuffer,
        VK_PIPELINE_BIND_POINT_GRAPHICS,
        *m_CrestRibbonPipeline
    );

    vkCmdBindDescriptorSets(
        commandBuffer,
        VK_PIPELINE_BIND_POINT_GRAPHICS,
        m_CrestRibbonPipeline->GetLayout(),
        0,
        1,
        &m_DescriptorSets[currentFrame],
        0,
        nullptr
    );

    vkCmdBindDescriptorSets(
        commandBuffer,
        VK_PIPELINE_BIND_POINT_GRAPHICS,
        m_CrestRibbonPipeline->GetLayout(),
        1,
        1,
        &m_AppearanceDescriptorSets[currentFrame],
        0,
        nullptr
    );

    VkBuffer vertexBuffers[] = {
        *m_CrestRibbonVertexBuffers[currentFrame]
    };

    VkDeviceSize offsets[] = {0};

    vkCmdBindVertexBuffers(
        commandBuffer,
        0,
        1,
        vertexBuffers,
        offsets
    );

    vkCmdDraw(
        commandBuffer,
        m_CrestRibbonVertexCount,
        1,
        0,
        0
    );
}

void Stage12FluidFluxApp::DrawQuadtreeTiles(
    VkCommandBuffer commandBuffer
)
{
    if(!m_WaterPatchMesh){
        return;
    }

    if(m_CurrentVisibleWaterTileCount == 0){
        return;
    }

    m_WaterPatchMesh->Bind(commandBuffer);

    m_WaterPatchMesh->DrawInstanced(
        commandBuffer,
        m_CurrentVisibleWaterTileCount
    );
}

void Stage12FluidFluxApp::Render(VkCommandBuffer commandBuffer, uint32_t imageIndex)
{
    // 在录制命令前的安全点执行重烘焙：此处不在任何命令缓冲区录制中，
    // RebakeShoreField 内部 vkDeviceWaitIdle 后销毁/重建纹理不会使在飞命令失效
    if(m_ShoreRebakePending){
        RebakeShoreField();
        m_ShoreRebakePending = false;
    }

    if(m_BoreProfileRebuildPending){
        RebuildBoreProfileResources();
        m_BoreProfileRebuildPending = false;
    }

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;

    if(vkBeginCommandBuffer(commandBuffer, &beginInfo) != VK_SUCCESS){
        throw std::runtime_error("Failed to begin Stage6 command buffer");
    }

    uint32_t currentFrame = GetCurrentFrameIndex();

    VkRenderPassBeginInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    renderPassInfo.renderPass = GetRenderPass();
    renderPassInfo.framebuffer = GetSwapChain().GetFramebuffer(imageIndex);
    renderPassInfo.renderArea.offset = {0, 0};
    renderPassInfo.renderArea.extent = GetSwapChain().GetExtent();

    std::array<VkClearValue, 2> clearValues{};
    clearValues[0].color.float32[0] = 0.02f;
    clearValues[0].color.float32[1] = 0.03f;
    clearValues[0].color.float32[2] = 0.05f;
    clearValues[0].color.float32[3] = 1.0f;
    clearValues[1].depthStencil = {1.0f, 0};

    renderPassInfo.clearValueCount = static_cast<uint32_t>(clearValues.size());
    renderPassInfo.pClearValues = clearValues.data();

    // Compute Pass（计算 Pass） 触发 Compute Shader 完成 FFT 波浪模拟的全部 GPU 工作
    m_TessendorfGPU->UpdateGPU(
        commandBuffer,
        currentFrame,
        m_LastSimulationDeltaTime
    );
    
    // Foam Compute Pass （泡沫计算 Pass） 触发 Compute Shader 完成泡沫模拟的全部 GPU 工作
        // foam_source.comp：根据涌潮剖面和 FFT 结果计算出泡沫源和流速场。
        // foam_advect.comp：利用上一帧的泡沫状态、源和流速，求解平流‑扩散方程，更新泡沫状态图。
    RecordFoamSimulation(
        commandBuffer,
        currentFrame
    );

    // Bore Wake Compute Pass
    RecordBoreWakeSimulation(
        commandBuffer, 
        currentFrame
    );
    
    // Graphics Pass（图形 Pass） 触发 Vertex Shader 和 Fragment Shader 完成网格绘制
    vkCmdBeginRenderPass(
        commandBuffer,
        &renderPassInfo,
        VK_SUBPASS_CONTENTS_INLINE
    );

    VkViewport viewport{};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = static_cast<float>(GetSwapChain().GetExtent().width);
    viewport.height = static_cast<float>(GetSwapChain().GetExtent().height);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;

    vkCmdSetViewport(commandBuffer, 0, 1, &viewport);

    VkRect2D scissor{};
    scissor.offset = {0, 0};
    scissor.extent = GetSwapChain().GetExtent();

    vkCmdSetScissor(commandBuffer, 0, 1, &scissor);

    if(m_SkyPipeline){
        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
            *m_SkyPipeline);
        vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
            m_SkyPipeline->GetLayout(), 0, 1,
            &m_DescriptorSets[currentFrame], 0, nullptr);
        vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
            m_SkyPipeline->GetLayout(), 1, 1,
            &m_AppearanceDescriptorSets[currentFrame], 0, nullptr);
        vkCmdDraw(commandBuffer, 3, 1, 0, 0);
    }

    // 地形是不透明的，必须先画：半透明水体做 alpha 混合时需要它已在 framebuffer 中
    if(m_TerrainPipeline && m_TerrainGrid){
        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
            *m_TerrainPipeline);
        vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
            m_TerrainPipeline->GetLayout(), 0, 1,
            &m_DescriptorSets[currentFrame], 0, nullptr);
        vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
            m_TerrainPipeline->GetLayout(), 1, 1,
            &m_AppearanceDescriptorSets[currentFrame], 0, nullptr);
        m_TerrainGrid->Bind(commandBuffer);
        m_TerrainGrid->Draw(commandBuffer);
    }

    vkp::Pipeline* pipeline = m_SolidPipeline.get();

    if(m_UseWireframe && m_WireframePipeline){
        pipeline = m_WireframePipeline.get();
    }

    vkCmdBindPipeline(
        commandBuffer,
        VK_PIPELINE_BIND_POINT_GRAPHICS,
        *pipeline
    );

    vkCmdBindDescriptorSets(
        commandBuffer,
        VK_PIPELINE_BIND_POINT_GRAPHICS,
        pipeline->GetLayout(),
        0,
        1,
        &m_DescriptorSets[currentFrame],
        0,
        nullptr
    );

    vkCmdBindDescriptorSets(
        commandBuffer,
        VK_PIPELINE_BIND_POINT_GRAPHICS,
        m_SolidPipeline->GetLayout(),
        1,
        1,
        &m_AppearanceDescriptorSets[currentFrame],
        0,
        nullptr
    );

    // 绘制多个 Quadtree Tile，而不是一个固定 Grid
    // 注意：DrawQuadtreeTiles 依赖上面绑定的 m_SolidPipeline，不自绑管线，
    // 因此地形绘制（会切换到 m_TerrainPipeline）必须放在水体 tile 之后。
    DrawQuadtreeTiles(commandBuffer);

    DrawCrestRibbon(commandBuffer, currentFrame);

    if(m_GuiEnabled){
        DrawGui();
        gui::Render(commandBuffer);
    }

    vkCmdEndRenderPass(commandBuffer);

    if(vkEndCommandBuffer(commandBuffer) != VK_SUCCESS){
        throw std::runtime_error("Failed to record Stage6 command buffer");
    }
}

void Stage12FluidFluxApp::SetupGui()
{
    auto poolSizes = gui::GetDescriptorPoolSizes();

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    poolInfo.maxSets = 1000 * static_cast<uint32_t>(poolSizes.size());
    poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
    poolInfo.pPoolSizes = poolSizes.data();

    if(vkCreateDescriptorPool(GetDevice(), &poolInfo, nullptr, &m_GuiDescriptorPool) != VK_SUCCESS){
        throw std::runtime_error("Failed to create ImGui descriptor pool");
    }

    gui::Init(
        GetInstance(),
        GetPhysicalDevice().GetHandle(),
        GetDevice(),
        GetDevice().GetGraphicsQueueFamily(),
        GetDevice().GetGraphicsQueue(),
        m_GuiDescriptorPool,
        2,
        static_cast<uint32_t>(GetSwapChain().GetImageCount()),
        GetWindow().GetNativeWindow(),
        GetRenderPass()
    );

    gui::UploadFonts(
        GetDevice(),
        GetDevice().GetGraphicsQueue(),
        GetCommandPool()
    );
}

void Stage12FluidFluxApp::RebuildQuadtreeFromGui()
{
    vkDeviceWaitIdle(GetDevice());
    m_WaterQuadtree.reset();
    m_WaterPatchMesh.reset();
    CreateWaterPatch();
}

void Stage12FluidFluxApp::ResetBoreEvent()
{
    m_BoreTime = 0.0f;
    m_BoreAccumulator = 0.0f;
    m_ProfileTime = m_BoreProfileConfig.duration * m_BoreProfileGui.fixedPhase;
    m_CurrentFoamStateIndex = 0;
    ResetMultiBoreEvents();
}

void Stage12FluidFluxApp::DrawGui()
{
    gui::NewFrame();

    ImGui::SetNextWindowSize(ImVec2(430.0f, 680.0f), ImGuiCond_FirstUseEver);

    if(!ImGui::Begin("Stage 12 Fluid Flux", &m_GuiEnabled)){
        ImGui::End();
        return;
    }

    ImGuiIO& io = ImGui::GetIO();
    ImGui::Text("Frame %.3f ms (%.1f FPS)", 1000.0f / io.Framerate, io.Framerate);
    ImGui::Text("Tiles: %u", m_CurrentVisibleWaterTileCount);

    if(ImGui::CollapsingHeader("Debug - 调试开关：控制暂停、线框、功能开关和 shader 可视化模式", ImGuiTreeNodeFlags_DefaultOpen)){
        ImGui::Checkbox("Wireframe - 线框显示水面网格密度", &m_UseWireframe);
        ImGui::Checkbox("Pause All - 暂停整体时间推进", &m_Paused);
        ImGui::SameLine();
        if(ImGui::Button("Step - 单帧推进")){
            m_StepOnce = true;
        }
        ImGui::Checkbox("Bore Paused - 暂停潮头推进时间", &m_BorePaused);

        ImGui::Checkbox("FFT Enabled - 开关背景 FFT 海浪", &m_FFTEnabled);
        ImGui::Checkbox("Bore Enabled - 开关一线潮整体位移", &m_BoreEnabled);
        ImGui::Checkbox("Profile Enabled - 开关 Wave Profile 剖面效果", &m_ProfileEnabled);

        ImGui::SliderInt("Debug Mode - 片元 shader 输出模式", &m_DebugMode, 0, 47);
        ImGui::Text("%s", DebugModeName(m_DebugMode));

        if(ImGui::Button("Final")){
            m_DebugMode = 0;
        }
        if(ImGui::Button("Height 高度场灰度图")){
            m_DebugMode = 1;
        }
    }

    if(ImGui::CollapsingHeader("FFT Ocean - 背景海浪：控制三层 FFT 波长范围和振幅权重")){
        ImGui::DragFloat("Short Patch - 短波纹理周期/高频细节尺度", &m_OceanConfig.spectrum.shortPatchLength, 1.0f, 1.0f, 512.0f);
        ImGui::DragFloat("Mid Patch - 中波纹理周期/主体波浪尺度", &m_OceanConfig.spectrum.midPatchLength, 1.0f, 1.0f, 2048.0f);
        ImGui::DragFloat("Long Patch - 长波纹理周期/大尺度涌浪范围", &m_OceanConfig.spectrum.longPatchLength, 1.0f, 1.0f, 4096.0f);
        ImGui::SliderFloat("Short Amp - 短波振幅权重", &m_OceanConfig.amplitudeScales[0], 0.0f, 10.0f);
        ImGui::SliderFloat("Mid Amp - 中波振幅权重", &m_OceanConfig.amplitudeScales[1], 0.0f, 10.0f);
        ImGui::SliderFloat("Long Amp - 长波振幅权重", &m_OceanConfig.amplitudeScales[2], 0.0f, 10.0f);
        ImGui::Text("Resolution/random seed require FFT resource rebuild.");
    }

    if(ImGui::CollapsingHeader("Bore Front - 一线潮波前：控制潮头沿河推进的速度、位置和开关")){
        int fieldMode = static_cast<int>(m_BoreFieldMode);
        ImGui::Text("Bore Field Mode - 潮头场计算方式");
        ImGui::RadioButton("SDF + FlowMap (旧双贴图)", &fieldMode, 0);
        ImGui::RadioButton("Progress Field (新单贴图)", &fieldMode, 1);
        m_BoreFieldMode = static_cast<BoreFieldMode>(fieldMode);
        
        // ImGui::Checkbox("Multi Bore Enabled - 启用多潮头事件系统", &m_MultiBoreGui.enabled);
        ImGui::Text("Active events - 当前活跃潮头: %u / %u", m_BoreEventManager.GetActiveCount(), water::kMaxBoreEvents);
        ImGui::Text("Next spawn - 下次生成倒计时: %.2f s", m_BoreEventManager.GetSpawnCountdown());
        ImGui::DragInt("Random Seed - 随机种子", &m_MultiBoreGui.seed, 1.0f, 1, 999999);
        ImGui::DragFloat("Spawn Min - 最短生成间隔 s", &m_MultiBoreGui.minSpawnInterval, 0.1f, 0.1f, 60.0f);
        ImGui::DragFloat("Spawn Max - 最长生成间隔 s", &m_MultiBoreGui.maxSpawnInterval, 0.1f, 0.1f, 60.0f);
        ImGui::DragFloat("Retry Min - 间距不足时最短重试 s", &m_MultiBoreGui.retryMinInterval, 0.05f, 0.05f, 10.0f);
        ImGui::DragFloat("Retry Max - 间距不足时最长重试 s", &m_MultiBoreGui.retryMaxInterval, 0.05f, 0.05f, 10.0f);
        ImGui::DragFloat("Base Speed - 新潮头基础速度 m/s", &m_MultiBoreGui.baseSpeed, 0.1f, 0.0f, 80.0f);
        ImGui::DragFloat("Remove Margin - 河尾回收余量 m", &m_MultiBoreGui.removeMargin, 1.0f, 0.0f, 500.0f);
        ImGui::DragFloat("Separation Padding - 最小间距额外 padding m", &m_MultiBoreGui.minimumSeparationPadding, 1.0f, 0.0f, 200.0f);
        ImGui::SliderFloat("Bore Lateral Extent - 潮头横向覆盖", &m_MultiBoreGui.lateralExtent, 0.2f, 1.0f);
        ImGui::SliderFloat("Bore Lateral Fade - 两岸淡出", &m_MultiBoreGui.lateralFade, 0.01f, 0.5f);
        if(ImGui::Button("Reset Multi Bore - 重置多潮头事件")){
            ResetMultiBoreEvents();
        }
        ImGui::SameLine();
        if(ImGui::Button("Spawn One - 立刻生成一个潮头")){
            m_BoreEventManager.SpawnManual(BuildBoreEventManagerConfig());
        }

        ImGui::Separator();
        // ImGui::Checkbox("（已弃用）Use Front LUT - 使用旧 LUT 横向波前扰动", &m_BoreUseLUT);
        ImGui::Checkbox("Debug Ridge - 启用调试浪脊增强", &m_BoreDebugRidgeEnabled);
        // ImGui::DragFloat2("（已弃用）Origin - 旧直线潮头世界起点 XZ", glm::value_ptr(m_BoreFrontParams.origin), 1.0f);
        // ImGui::DragFloat2("（已弃用）Direction - 旧直线潮头推进方向 XZ", glm::value_ptr(m_BoreFrontParams.direction), 0.01f, -1.0f, 1.0f);
        // ImGui::DragFloat("Speed - 潮头沿河推进速度 m/s", &m_BoreFrontParams.speed, 1.0f, 0.0f, 300.0f);
        // ImGui::DragFloat("Initial Offset - 初始沿河进度偏移 m", &m_BoreFrontParams.initialOffset, 1.0f, -500.0f, 5000.0f);
        // ImGui::DragFloat("（已弃用）Front Length - 旧 LUT 波前横向长度 m", &m_BoreFrontParams.frontLength, 10.0f, 1.0f, 5000.0f);
        // ImGui::SliderFloat("（已弃用）Edge Fade - 旧 LUT 波前两端淡出比例", &m_BoreFrontParams.edgeFadeFraction, 0.0f, 0.5f);
        ImGui::DragFloat("Bore Time - 当前潮头累计时间 s", &m_BoreTime, 0.1f, 0.0f, 200.0f);

        if(ImGui::Button("Reset Bore - 重置潮头和泡沫状态")){
            ResetBoreEvent();
        }
    }

    if(ImGui::CollapsingHeader("Crest Noise - 浪脊噪声：打破一堵墙，控制大起伏/细碎波动/顶边参差")){
        ImGui::TextWrapped("整体振幅调制(改高度+泡沫)");
        ImGui::SliderFloat("Lateral Freq - 横向(河宽)团块数", &m_CrestNoiseGui.lateralFrequency, 0.0f, 24.0f);
        ImGui::SliderFloat("Along Freq X - 沿河频率X", &m_CrestNoiseGui.alongFrequencyX, 0.0f, 0.2f, "%.4f");
        ImGui::SliderFloat("Along Freq Y - 沿河频率Y", &m_CrestNoiseGui.alongFrequencyY, 0.0f, 0.2f, "%.4f");
        ImGui::SliderFloat("Anim Speed - 随潮头流动速度", &m_CrestNoiseGui.animationSpeed, 0.0f, 0.5f, "%.4f");
        ImGui::SliderFloat("Detail Freq - 细碎波动频率", &m_CrestNoiseGui.detailFrequency, 0.0f, 32.0f);
        ImGui::SliderFloat("Detail Weight - 大/细占比", &m_CrestNoiseGui.detailWeight, 0.0f, 1.0f);
        ImGui::SliderFloat("Amplitude Min - 振幅下限", &m_CrestNoiseGui.amplitudeMin, 0.0f, 1.0f);
        ImGui::SliderFloat("Amplitude Max - 振幅上限", &m_CrestNoiseGui.amplitudeMax, 1.0f, 5.0f);
        ImGui::Separator();
        ImGui::TextWrapped("浪脊顶边参差(只改高度)");
        ImGui::SliderFloat("Wobble Strength - 顶抖强度", &m_CrestNoiseGui.wobbleStrength, 0.0f, 1.5f);
        ImGui::SliderFloat("Wobble Freq - 顶抖频率", &m_CrestNoiseGui.wobbleFrequency, 0.5f, 12.0f);
    }

    if(ImGui::CollapsingHeader("Bore Profile - 潮头剖面：控制 Wave Profile 的宽度、高度、前向推挤和水位抬升")){
        ImGui::Checkbox("Profile Paused - 固定剖面动画相位", &m_ProfilePaused);
        ImGui::Checkbox("Auto Repeat - 自动重复触发潮头事件", &m_AutoRepeatEvent);
        ImGui::DragFloat("Profile Half Width - 剖面半宽/潮头影响距离 m", &m_BoreProfileConfig.profileHalfWidth, 0.5f, 1.0f, 240.0f);
        ImGui::DragFloat("Crest Width - 波峰前缘宽度 m", &m_BoreProfileConfig.crestWidth, 0.2f, 4.0f, 80.0f);
        ImGui::DragFloat("Rear Slope Length - 波后平滑回落长度 m", &m_BoreProfileConfig.rearSlopeLength, 0.5f, 10.0f, 220.0f);
        ImGui::DragFloat("Rear Trough Depth - 后坡下凹深度 m", &m_BoreProfileConfig.rearTroughDepth, 0.02f, 0.0f, 3.0f);
        ImGui::DragFloat("Hydraulic Rise In Profile - 剖面内部水跃台阶", &m_BoreProfileConfig.hydraulicRiseScale, 0.02f, 0.0f, 1.0f);
        if(ImGui::Button("Rebuild Bore Profile Textures - 重建潮头剖面纹理")){
            m_BoreProfileRebuildPending = true;
        }
        ImGui::DragFloat("Duration - 剖面完整动画时长 s", &m_BoreProfileConfig.duration, 0.1f, 0.1f, 120.0f);
        ImGui::SliderFloat("Fixed Phase - 固定采样相位 0~1", &m_BoreProfileGui.fixedPhase, 0.0f, 1.0f);
        ImGui::DragFloat("Water Rise - 潮后整体水位抬升高度 m", &m_BoreProfileGui.waterRiseHeight, 0.1f, 0.0f, 20.0f);
        ImGui::DragFloat("Rise Width - 水位抬升过渡宽度 m", &m_BoreProfileGui.riseWidth, 0.1f, 0.0f, 160.0f);
        ImGui::DragFloat("Global Amplitude - 潮头整体振幅倍数", &m_BoreProfileGui.globalAmplitude, 0.05f, 0.0f, 10.0f);
        ImGui::DragFloat("Forward Scale - 水平前向推挤强度", &m_BoreProfileGui.forwardScale, 0.05f, 0.0f, 20.0f);
        ImGui::DragFloat("Upward Scale - 垂直抬升/浪高强度", &m_BoreProfileGui.upwardScale, 0.1f, 0.0f, 20.0f);
        ImGui::SliderFloat("Active Region - 潮头区域总开关掩码", &m_BoreProfileGui.activeRegionMask, 0.0f, 1.0f);
        ImGui::DragFloat3("FFT Suppression - 潮头处短/中/长波抑制", glm::value_ptr(m_BoreProfileGui.suppression), 0.01f, 0.0f, 1.0f);
    }

    if(ImGui::CollapsingHeader("Foam - 泡沫：控制潮头泡沫、FFT 破碎泡沫和状态泡沫输运")){
        ImGui::DragFloat("Animation Cycle - 三相泡沫细节循环周期 s", &m_FoamGui.animationCycle, 0.1f, 0.1f, 20.0f);
        ImGui::DragFloat("Detail Scale - 泡沫细节纹理世界缩放", &m_FoamGui.detailWorldScale, 0.001f, 0.001f, 1.0f);
        ImGui::DragFloat4("Source Strength - Profile/Slope/Jacobian/Breaking 泡沫源强度", glm::value_ptr(m_FoamGui.sourceStrength), 0.01f, 0.0f, 5.0f);
        ImGui::DragFloat4("Thresholds - 泡沫生成阈值和软化范围", glm::value_ptr(m_FoamGui.thresholds), 0.01f, 0.0f, 1.0f);
        ImGui::DragFloat4("Appearance - 覆盖阈值/软边/法线/状态混合", glm::value_ptr(m_FoamGui.appearance), 0.01f, 0.0f, 2.0f);
        ImGui::DragFloat("State Gain - 状态泡沫源项增益", &m_FoamGui.stateGain, 0.05f, 0.0f, 10.0f);
        ImGui::DragFloat("State Decay - 状态泡沫衰减速度", &m_FoamGui.stateDecay, 0.01f, 0.0f, 5.0f);
        ImGui::DragFloat("State Diffusion - 状态泡沫扩散系数", &m_FoamGui.stateDiffusion, 0.001f, 0.0f, 1.0f);
        ImGui::Checkbox("Foam Solver - 开关状态泡沫计算", &m_FoamGui.solverEnabled);
        ImGui::DragFloat4("Foam Domain - 状态泡沫世界范围 minX/minZ/sizeX/sizeZ", glm::value_ptr(m_FoamGui.domain), 1.0f);
        ImGui::Separator();
        ImGui::TextWrapped("FFT 全局海洋泡沫距离淡出(压掉潮外远处的网格泡沫)。想彻底关掉海洋泡沫可把 Source Strength 的 Slope/Jacobian 两项调 0。");
        ImGui::DragFloat("Ocean Foam Fade Near - 开始淡出距离 m", &m_FoamGui.oceanFoamFadeNear, 1.0f, 0.0f, 2000.0f);
        ImGui::DragFloat("Ocean Foam Fade Far - 完全消失距离 m", &m_FoamGui.oceanFoamFadeFar, 1.0f, 0.0f, 4000.0f);
    
        ImGui::SeparatorText("FF Foam - 双层泡沫（MF_FluidFoam）");
        ImGui::SliderFloat("Shallow Offset - 浅水偏置", &m_FoamGui.foamShallowOffset, -0.5f, 1.0f);
        ImGui::SliderFloat("Shallow Scale - 浅水淡出(1/米)", &m_FoamGui.foamShallowScale, 0.05f, 10.0f);
        ImGui::SliderFloat("Hardness Intensity - 硬核强度", &m_FoamGui.foamHardnessIntensity, -1.0f, 1.0f);
        ImGui::SliderFloat("Hardness Width - 硬核宽度", &m_FoamGui.foamHardnessWidth, 0.02f, 0.95f);
        ImGui::SliderFloat("Soft Velocity - 软晕随流速", &m_FoamGui.foamSoftVelocity, 0.0f, 3.0f);
        ImGui::SliderFloat("Soft Base - 软晕基底", &m_FoamGui.foamSoftBase, 0.0f, 2.0f);
        ImGui::SliderFloat("Soft Max - 软晕上限", &m_FoamGui.foamSoftMax, 0.0f, 3.0f);
        ImGui::SliderFloat("Foam Alpha - 泡沫总不透明度", &m_FoamGui.foamAlpha, 0.0f, 1.0f);
    }

    if(ImGui::CollapsingHeader("Crest Ribbon - 独立白色潮脊")){
        ImGui::Checkbox("Enable Crest Ribbon - 启用浪脊带", &m_CrestRibbonGui.enabled);
        ImGui::SliderInt("Ribbon Lateral Segments - 横向分段数", &m_CrestRibbonGui.lateralSegments, 64, 512);
        ImGui::SliderInt("Ribbon Depth Segments - 纵向分段数", &m_CrestRibbonGui.depthSegments, 2, 8);
        ImGui::SliderFloat("Ribbon Front Width - 浪脊前缘宽度", &m_CrestRibbonGui.frontWidth, 2.0f, 60.0f);
        ImGui::SliderFloat("Ribbon Wake Width - 浪脊尾迹宽度", &m_CrestRibbonGui.wakeWidth, 40.0f, 500.0f);
        ImGui::SliderFloat("Ribbon Height Offset - 高度偏移", &m_CrestRibbonGui.heightOffset, 0.0f, 2.0f);
        ImGui::SliderFloat("Ribbon Crest Height Offset - 浪峰高度偏移", &m_CrestRibbonGui.crestHeightOffset, 0.0f, 3.0f);
        ImGui::SliderFloat("Ribbon Alpha - 透明度", &m_CrestRibbonGui.alpha, 0.0f, 1.0f);
        ImGui::SliderFloat("Ribbon Edge Fade - 边缘淡出", &m_CrestRibbonGui.edgeFade, 0.01f, 0.4f);

        ImGui::SeparatorText("Ribbon Irregular Wake - 无规则白水");
        ImGui::SliderFloat("Curve Meters - 主潮线基础弯曲", &m_CrestRibbonGui.curveMeters, 0.0f, 260.0f);
        ImGui::SliderFloat("Irregular Curve - 不规则弯曲", &m_CrestRibbonGui.irregularCurveMeters, 0.0f, 140.0f);
        ImGui::SliderFloat("Curve Frequency - 弯曲频率", &m_CrestRibbonGui.curveFrequency, 0.2f, 4.0f);
        ImGui::SliderFloat("Height Variation - 高度起伏", &m_CrestRibbonGui.heightVariation, 0.0f, 2.0f);

        ImGui::SliderFloat("Edge Jitter - 前沿抖动(米)", &m_CrestRibbonGui.edgeJitterMeters, 0.0f, 40.0f);
        ImGui::SliderFloat("Wake Patch Threshold - 泡沫团阈值", &m_CrestRibbonGui.wakePatchThreshold, 0.25f, 0.85f);
        ImGui::SliderFloat("Wake Foam Strength - 泡沫团强度", &m_CrestRibbonGui.wakeFoamStrength, 0.0f, 5.0f);
        ImGui::SliderFloat("Wake Hole Strength - 孔洞强度", &m_CrestRibbonGui.wakeHoleStrength, 0.0f, 1.0f);
    
        ImGui::SliderFloat("Hard Crest Width - 主白线宽度(米)", &m_CrestRibbonGui.hardCrestWidth, 2.0f, 60.0f);
        ImGui::SliderFloat("Wake Start - 浮沫开始距离(米)", &m_CrestRibbonGui.wakeStart, 0.0f, 120.0f);
        ImGui::SliderFloat("Wake End - 浮沫结束距离(米)", &m_CrestRibbonGui.wakeEnd, 40.0f, 800.0f);
        ImGui::SliderFloat("Wake Feather - 浮沫软边宽度(米)", &m_CrestRibbonGui.wakeFeather, 5.0f, 200.0f);
    }

    if(ImGui::CollapsingHeader("Bore Wake State - 状态型含气白水")){
        ImGui::Checkbox("Enable Bore Wake - 启用涌潮尾流白水", &m_BoreWakeGui.enabled);
        // ImGui::SliderInt("Wake Resolution - 白水纹理分辨率", &m_BoreWakeGui.resolution, 256, 2048);
        if(ImGui::Button("Clear Bore Wake State - 清空白水状态")){
            InitializeBoreWakeImages();
            m_CurrentBoreWakeStateIndex = 0;
        }

        ImGui::SeparatorText("Range - 白水区间");
        ImGui::SliderFloat("Wake Start - 白水起始距离(米)", &m_BoreWakeGui.wakeStart, -50.0f, 150.0f);
        ImGui::SliderFloat("Wake End - 白水结束距离(米)", &m_BoreWakeGui.wakeEnd, 50.0f, 900.0f);
        ImGui::SliderFloat("Wake Feather - 白水边缘柔化宽度(米)", &m_BoreWakeGui.wakeFeather, 10.0f, 250.0f);
        ImGui::SliderFloat("Advection Speed - 向后平流速度(米/秒)", &m_BoreWakeGui.advectionSpeed, 0.0f, 180.0f);

        ImGui::SeparatorText("Source Strength - 源项强度");
        ImGui::SliderFloat("Source Strength - 全局源强度倍率", &m_BoreWakeGui.sourceStrength, 0.0f, 3.0f);
        ImGui::SliderFloat("Aeration Strength - 含气水(大气泡)强度", &m_BoreWakeGui.aerationStrength, 0.0f, 3.0f);
        ImGui::SliderFloat("Foam Strength - 细泡沫强度", &m_BoreWakeGui.foamStrength, 0.0f, 3.0f);
        ImGui::SliderFloat("Sediment Strength - 泥沙(棕黄浑浊)强度", &m_BoreWakeGui.sedimentStrength, 0.0f, 2.0f);
        ImGui::SliderFloat("Turbulence Strength - 湍流(水气混合)强度", &m_BoreWakeGui.turbulenceStrength, 0.0f, 2.0f);

        ImGui::SeparatorText("Decay - 衰减速度");
        ImGui::SliderFloat("Aeration Decay - 含气水衰减速率", &m_BoreWakeGui.aerationDecay, 0.0f, 1.0f);
        ImGui::SliderFloat("Foam Decay - 细泡沫衰减速率", &m_BoreWakeGui.foamDecay, 0.0f, 0.5f);
        ImGui::SliderFloat("Sediment Decay - 泥沙衰减速率", &m_BoreWakeGui.sedimentDecay, 0.0f, 0.2f);
        ImGui::SliderFloat("Turbulence Decay - 湍流衰减速率", &m_BoreWakeGui.turbulenceDecay, 0.0f, 1.0f);

        ImGui::SeparatorText("Noise - 白水团形态");
        ImGui::SliderFloat("Patch Threshold - 泡沫团块密度阈值", &m_BoreWakeGui.patchThreshold, 0.25f, 0.85f);
        ImGui::SliderFloat("Warp Strength - 噪声扭曲强度", &m_BoreWakeGui.warpStrength, 0.0f, 4.0f);
        ImGui::SliderFloat("Lateral Frequency - 横向噪声频率", &m_BoreWakeGui.lateralFrequency, 0.2f, 8.0f);
        ImGui::SliderFloat("Back Frequency - 纵向噪声频率", &m_BoreWakeGui.backFrequency, 1.0f, 16.0f);
    }

    if(ImGui::CollapsingHeader("Water Material - 水体材质：控制颜色、反射、高光、泥沙和远景雾")){
        ImGui::ColorEdit3("Shallow - 浅水颜色 RGB", glm::value_ptr(m_WaterMaterialGui.shallowColor));
        ImGui::ColorEdit3("Deep - 深水颜色 RGB", glm::value_ptr(m_WaterMaterialGui.deepColor));
        ImGui::ColorEdit3("Sediment - 泥沙颜色 RGB", glm::value_ptr(m_WaterMaterialGui.sedimentColor));
        ImGui::DragFloat4("Optical - F0/反射/粗糙或吸收/泥沙量", glm::value_ptr(m_WaterMaterialGui.opticalParams), 0.01f, 0.0f, 4.0f);
        ImGui::DragFloat3("Sun Direction - 太阳方向 xyz", glm::value_ptr(m_WaterMaterialGui.sunDirection), 0.01f, -1.0f, 1.0f);
        ImGui::DragFloat("Specular - 太阳高光强度", &m_WaterMaterialGui.specularStrength, 0.05f, 0.0f, 10.0f);
        ImGui::DragFloat4("Fog - 雾起点/终点/地平线融合/保留", glm::value_ptr(m_WaterMaterialGui.fogParams), 1.0f, 0.0f, 8000.0f);
        ImGui::SliderFloat3("Absorption - RGB 每米吸收", &m_WaterMaterialGui.absorption.x, 0.0f, 1.0f);
        ImGui::SliderFloat("Bed Albedo - 河床反照率强度", &m_WaterMaterialGui.bedAlbedo, 0.0f, 2.0f);
        ImGui::SliderFloat("Max Visible Depth - 最大可见水深(米)", &m_WaterMaterialGui.maxVisibleDepth, 0.5f, 30.0f);
        ImGui::SliderFloat("Shallow Blend - 吸收总倍率（越大越快变不透明）", &m_WaterMaterialGui.shallowBlend, 0.0f, 2.0f);
        ImGui::SliderFloat("Depth Upward Blend - 俯视光程加成", &m_WaterMaterialGui.depthUpwardBlend, 0.0f, 4.0f);
        
        ImGui::SeparatorText("FF Shoreline - 岸线两档（MF_WaterTransition）");
        ImGui::SliderFloat3("Absorb Shore - 岸线档吸收/米", &m_WaterMaterialGui.absorptionShore.x, 0.0f, 1.0f);
        ImGui::SliderFloat3("Scatter Deep - 深水档散射/米", &m_WaterMaterialGui.scatteringDeep.x, 0.0f, 0.1f, "%.4f");
        ImGui::SliderFloat3("Scatter Shore - 岸线档散射/米", &m_WaterMaterialGui.scatteringShore.x, 0.0f, 0.1f, "%.4f");
        ImGui::SliderFloat("Scatter Gain - 散射增益", &m_WaterMaterialGui.scatterGain, 0.0f, 30.0f);
        ImGui::SliderFloat("Foam Scatter Scale - 泡沫散射倍数", &m_WaterMaterialGui.foamScatterScale, 0.0f, 12.0f);
        ImGui::SliderFloat("Shore Depth Norm - 深度归一(米)", &m_WaterMaterialGui.shoreDepthNorm, 1.0f, 60.0f);
        ImGui::SliderFloat("Shore Dist Norm - 离岸归一(米)", &m_WaterMaterialGui.shoreDistNorm, 10.0f, 600.0f);
        ImGui::ColorEdit3("Color Behind - 水下背景色调", glm::value_ptr(m_WaterMaterialGui.colorBehind));
        ImGui::DragFloat("Water Level - 水面基准高度", &m_WaterMaterialGui.waterLevel, 0.1f);
    
        ImGui::SeparatorText("FF Specular - 高光/粗糙度（MF_FluidWaterLayer）");
        ImGui::SliderFloat("Spec Bias - Fresnel 偏置", &m_WaterMaterialGui.specBias, 0.0f, 0.3f, "%.3f");
        ImGui::SliderFloat("Spec Scale - Fresnel 幅度", &m_WaterMaterialGui.specScale, 0.0f, 3.0f);
        ImGui::SliderFloat("Spec Power - Fresnel 指数", &m_WaterMaterialGui.specPower, 1.0f, 16.0f);
        ImGui::SliderFloat("Horizon Floor - 远景高光底噪", &m_WaterMaterialGui.specHorizonFloor, 0.0f, 1.0f);
        ImGui::SliderFloat("Horizon Distance - 地平线距离(米)", &m_WaterMaterialGui.specHorizonDistance, 1.0f, 200.0f);
        ImGui::SliderFloat("Horizon Offset - 相机高度系数", &m_WaterMaterialGui.specHorizonOffset, 0.0f, 30.0f);
        ImGui::SliderFloat("Rough From Fresnel - 掠射粗糙度", &m_WaterMaterialGui.roughFromFresnel, 0.0f, 1.0f);
        ImGui::SliderFloat("Rough Min - 最小粗糙度", &m_WaterMaterialGui.roughMin, 0.01f, 0.5f, "%.3f");
        ImGui::SliderFloat("Scatter Details - 散射细节权重", &m_WaterMaterialGui.scatterDetails, 0.0f, 1.0f);
        ImGui::SliderFloat("Scatter Power - 散射指数", &m_WaterMaterialGui.scatterPower, 0.5f, 8.0f);
        ImGui::SliderFloat("Scatter Scale - 散射强度", &m_WaterMaterialGui.scatterScale, 0.0f, 8.0f);
        ImGui::SliderFloat("Normal Fix - 不可能法线修正强度", &m_WaterMaterialGui.normalFixStrength, 0.0f, 1.0f);
    }

    if(ImGui::CollapsingHeader("Quadtree LOD - 四叉树细分：控制水面覆盖范围、最细层级和屏幕误差阈值")){
        ImGui::DragFloat2("Root Center - 四叉树根节点中心 XZ", glm::value_ptr(m_QuadtreeGui.rootCenter), 1.0f);
        ImGui::DragFloat("Root Size - 四叉树根节点边长 m", &m_QuadtreeGui.rootSize, 16.0f, 128.0f, 32768.0f);
        ImGui::SliderInt("Max Level - 最大细分层级", &m_QuadtreeGui.maxLevel, 1, 10);
        ImGui::SliderInt("Patch Cells - WaterPatchMesh的网格数", &m_QuadtreeGui.patchCellCount, 8, 128);
        ImGui::DragFloat("FOV Y - 垂直视场角", &m_QuadtreeGui.fovYDegrees, 1.0f, 10.0f, 120.0f);
        ImGui::DragFloat("Split Pixels - 分裂阈值", &m_QuadtreeGui.splitPixels, 0.25f, 1.0f, 64.0f);
        ImGui::DragFloat("Merge Pixels - 合并阈值", &m_QuadtreeGui.mergePixels, 0.25f, 1.0f, 64.0f);
        ImGui::DragFloat("Min Y - 水面 AABB 的最小 Y 坐标（米）", &m_QuadtreeGui.minY, 0.5f, -100.0f, 0.0f);
        ImGui::DragFloat("Max Y - 水面 AABB 的最大 Y 坐标（米）", &m_QuadtreeGui.maxY, 0.5f, 0.0f, 100.0f);

        ImGui::SeparatorText("Bore LOD - 潮头局部细分");
        ImGui::SliderInt("Bore Core Level - 潮头核心 LOD 层级", &m_QuadtreeGui.boreCoreLevel, 0, m_QuadtreeGui.maxLevel);
        ImGui::SliderInt("Bore Near Level - 潮头附近过渡 LOD 层级", &m_QuadtreeGui.boreNearLevel, 0, m_QuadtreeGui.maxLevel);
        ImGui::SliderFloat("Bore Core Width - 潮头核心范围(米)", &m_QuadtreeGui.boreCoreWidth, 0.0f, 300.0f);
        ImGui::SliderFloat("Bore Near Width - 潮头附近过渡范围(米)", &m_QuadtreeGui.boreNearWidth, 0.0f, 700.0f);
        ImGui::SliderInt("Bore Ultra Level - 潮头超精细 LOD 层级", &m_QuadtreeGui.boreUltraLevel, 0, m_QuadtreeGui.maxLevel);
        ImGui::SliderFloat("Bore Ultra Width - 潮头超精细范围(米)", &m_QuadtreeGui.boreUltraWidth, 0.0f, 80.0f);

        if(ImGui::Button("Apply Quadtree Rebuild")){
            RebuildQuadtreeFromGui();
        }
    }

    if(ImGui::CollapsingHeader("River / Flow Map - 弯曲河道：调整弯曲河道涌潮的视觉表现")){
        ImGui::DragFloat("River Bore Curvature - 河道涌潮曲率", &m_RiverBoreCurvatureMeters, 0.25f, 0.0f, 50.0f);
        ImGui::Text("Flow Map control points require river resource rebuild.");
        ImGui::Text("Current river length - 当前河流中轴线的总长度: %.1f m", m_RiverLength);
    }

    if(ImGui::CollapsingHeader("Shore Field - 岸线场：湿润带/沙滩/岸上地形烘焙参数（改后需 Rebake）")){
        ImGui::SliderFloat("Wet Runup - 岸上湿润带延伸(米)", &m_ShoreParams.wetRunup, 0.0f, 120.0f);
        ImGui::SliderFloat("Sand Width - 沙滩影响半径(米)", &m_ShoreParams.sandWidth, 0.0f, 200.0f);
        ImGui::SliderFloat("Beach Slope - 岸上地形坡度", &m_ShoreParams.beachSlope, 0.0f, 1.0f, "%.3f");
        ImGui::SliderFloat("Max Beach Height - 岸上地形最大抬升(米)", &m_ShoreParams.maxBeachHeight, 0.0f, 60.0f);
        ImGui::SliderFloat("Terrain Height Scale - heightmap[0,1]→米", &m_ShoreParams.terrainHeightScale, 0.0f, 80.0f);
        ImGui::SliderFloat("River Bed Depth - 河床相对水面下沉(米)", &m_ShoreParams.riverBedDepth, 0.0f, 40.0f);
        ImGui::SliderFloat("River Bed Fade - 河床下沉过渡宽度(米)", &m_ShoreParams.riverBedFade, 1.0f, 3000.0f);
        if(ImGui::Button("Rebake Shore Field - 重新烘焙岸线场")){
            m_ShoreRebakePending = true;
        }
    }

    ImGui::End();
}

void Stage12FluidFluxApp::UpdateWindowTitle()
{
    m_TitleUpdateTimer += 1.0f / 60.0f;

    if(m_TitleUpdateTimer < 0.5f){
        return;
    }

    m_TitleUpdateTimer = 0.0f;

    const glm::vec3& pos = m_Camera.GetPosition();
    const glm::vec3& target = m_Camera.GetTarget();

    std::ostringstream title;
    title << "Stage 12 - Fluid Flux | " 
        << "pos=(" << pos.x << ", " << pos.y << ", " << pos.z
        << ") lookAt=(" << target.x << ", " << target.y << ", " << target.z  
        << ") mode=" << m_DebugMode
        << " wire=" << (m_UseWireframe ? "on" : "off")
        << " paused=" << (m_Paused ? "yes" : "no")
        << " tiles=" << m_CurrentVisibleWaterTileCount
        << "   --- Move Camera: WASD LeftCtrl Space ---";

    GetWindow().SetTitle(title.str());
}

void Stage12FluidFluxApp::OnFramebufferResize(int width, int height)
{
    core::Application::OnFramebufferResize(width, height);
}

void Stage12FluidFluxApp::OnKey(int key, int scancode, int action, int mods)
{
    // 保留基类的默认按键行为（如 ESC 退出等）
    core::Application::OnKey(key, scancode, action, mods);

    if(m_GuiEnabled){
        ImGuiIO& io = ImGui::GetIO();
        if(io.WantCaptureKeyboard){
            return;
        }
    }

    // 忽略非法键值，防止数组越界
    if(key < 0 || key >= 1024){
        return;
    }

    if(action == GLFW_PRESS){
        m_Keys[key] = true;

        // F1：基础水面光照（片段着色器 mode = 0）
        // 显示带简单漫反射光照的水面颜色，用于评估波浪几何体在光照下的真实感
        if(key == GLFW_KEY_F1){
            m_DebugMode = 0;
        }

        // F2：高度场灰度图（片段着色器 mode = 1）
        // 将 FFT 波浪高度映射为灰度，亮处为波峰、暗处为波谷，检查波浪垂直位移分布
        if(key == GLFW_KEY_F2){
            m_DebugMode = 1;
        }

        // F3：水平位移可视化（片段着色器 mode = 2）
        // 用红蓝通道分别显示 X/Z 方向的水平位移（choppy），中间灰表示无偏移
        if(key == GLFW_KEY_F3){
            m_DebugMode = 2;
        }

        // F4：法线扰动斜率可视化（片段着色器 mode = 3）
        // 将法线辅助纹理中的 slopeX/slopeZ 映射到红/蓝通道，检验法线扰动是否正确
        if(key == GLFW_KEY_F4){
            m_DebugMode = 3;
        }

        // F5：波浪破碎/泡沫判据可视化（片段着色器 mode = 4）
        // 根据 Jacobian 行列式显示泡沫候选区域，越白表示越可能产生白浪
        if(key == GLFW_KEY_F5){
            m_DebugMode = 4;
        }

        // F6：世界空间法线可视化（片段着色器 mode = 5）
        // RGB 编码世界空间法线方向，绿 = 朝上，红/蓝 = 倾斜，纯色表示法线朝向异常
        if(key == GLFW_KEY_F6){
            m_DebugMode = 5;
        }

        // F7：波前带符号距离可视化（片段着色器 mode = 6）
        // 蓝红分区 + 白色波前峰线，检查涌潮推进方向、波前位置是否正确
        if(key == GLFW_KEY_F7){
            m_DebugMode = 6;
        }

        // F8：波前长度淡入淡出掩码（片段着色器 mode = 7）
        // 检查 1 km 波前两端的平滑消失效果，边缘应为渐变而非硬切
        if(key == GLFW_KEY_F8){
            m_DebugMode = 7;
        }

        // F9：标准化波前线坐标（片段着色器 mode = 8）
        // 检查 frontU 从 0 到 1 的映射是否正确，绿红渐变应沿波前切向变化
        if(key == GLFW_KEY_F9){
            m_DebugMode = 8;
        }

        // F10：局部波前法线方向（片段着色器 mode = 9）
        // 直线波前时法线应均匀一致，弯曲波前时法线应随 LUT 偏移旋转
        if(key == GLFW_KEY_F10){
            m_DebugMode = 9;
        }

        // F11：振幅乘数（片段着色器 mode = 10）
        // 检查 Front LUT 的 G 通道是否正确控制波高空间分布
        if(key == GLFW_KEY_F11){
            m_DebugMode = 10;
        }

        // F12：泡沫乘数（片段着色器 mode = 11）
        // 检查 Front LUT 的 B 通道是否正确控制泡沫强度空间分布
        if(key == GLFW_KEY_F12){
            m_DebugMode = 11;
        }

        if(key == GLFW_KEY_M){
            m_DebugMode = (m_DebugMode + 1) % 48;
        }

        // Tab：切换线框渲染模式
        // 在实体填充和三角形线框之间切换，用于观察水面网格密度和变形情况
        if(key == GLFW_KEY_TAB){
            m_UseWireframe = !m_UseWireframe;
        }

        // P：暂停/继续水面模拟
        // 暂停时波浪停止演化（时间不再推进），便于观察某一时刻的波形细节
        if(key == GLFW_KEY_P){
            m_Paused = !m_Paused;
        }

        // O：单步执行一帧
        // 按下后模拟前进一帧（固定 1/60 秒）然后暂停，用于逐帧调试波浪动画
        if(key == GLFW_KEY_O){
            m_StepOnce = true;
        }

        // R：重置涌潮波前时间
        // 将 BoreTime 归零，使一线潮回到初始位置（origin + initialOffset）
        if(key == GLFW_KEY_R){
            m_BoreTime = 0.0f;
            m_ProfileTime =
                m_BoreProfileConfig.duration *
                0.60f; // 固定 Profile 在成熟阶段
            m_CurrentFoamStateIndex = 0;
        }

        // C：切换 Front LUT 的使用
        // true = 使用弯曲波前（从 LUT 采样偏移量和法线修正），false = 直线波前
        if(key == GLFW_KEY_C){
            m_BoreUseLUT = !m_BoreUseLUT;
        }

        // B：整体开关涌潮效果
        // true = 叠加一线潮位移到水面网格，false = 仅显示背景 FFT 波浪
        if(key == GLFW_KEY_B){
            m_BoreEnabled = !m_BoreEnabled;
        }

        // T：切换涌潮浪尖掩码调试
        // 控制 ridgeStrength 参数，启用时浪尖区域会以 0.35 的强度增强泡沫/位移
        if(key == GLFW_KEY_T){
            m_BoreDebugRidgeEnabled = !m_BoreDebugRidgeEnabled;
        }

        // I：暂停/继续涌潮时间
        // 暂停时 BoreTime 不再累加，波前停在当前位置，但背景波浪仍可正常演化
        if(key == GLFW_KEY_I){
            m_BorePaused = !m_BorePaused;
        }

        // L：切换涌潮剖面动画模式（OneShot / Looping）
        // OneShot = 播放一次完整生命周期后停止
        // Looping = 循环播放，用于持续近岸破浪效果
        if(key == GLFW_KEY_L){
            if(m_ProfileMode == water::BoreProfileAnimationMode::OneShot){
                m_ProfileMode = water::BoreProfileAnimationMode::Looping;
            }
            else{
                m_ProfileMode = water::BoreProfileAnimationMode::OneShot;
            }
        }

        // K：暂停/继续剖面动画时间
        // 暂停时 ProfileTime 不再累加，翻卷动画冻结，便于观察特定帧的波形
        if(key == GLFW_KEY_K){
            m_ProfilePaused = !m_ProfilePaused;
        }

        // Y：切换自动重复事件模式
        // 启用后涌潮会按设定间隔自动重复触发 OneShot 事件
        // 用于模拟一阵一阵出现的涌潮
        if(key == GLFW_KEY_Y){
            m_AutoRepeatEvent = !m_AutoRepeatEvent;
        }

        // G：切换涌潮剖面效果开关
        // 关闭后只显示背景 FFT 波浪，不叠加 Wave Profile 位移
        if(key == GLFW_KEY_G){
            m_ProfileEnabled = !m_ProfileEnabled;
        }

        // H：切换 FFT 背景波浪开关
        // 关闭后只显示涌潮剖面，不显示背景波浪
        // 用于单独观察涌潮波形的几何形状
        if(key == GLFW_KEY_H){
            m_FFTEnabled = !m_FFTEnabled;
        }
    }
    else if(action == GLFW_RELEASE){
        m_Keys[key] = false;
    }   
}

void Stage12FluidFluxApp::OnMouseMove(double x, double y)
{
    if(m_GuiEnabled){
        ImGuiIO& io = ImGui::GetIO();
        if(io.WantCaptureMouse){
            m_FirstMouse = true;
            return;
        }
    }

    if(!m_CameraControlEnabled){
        return;
    }

    if(m_FirstMouse){
        m_LastMouseX = x;
        m_LastMouseY = y;
        m_FirstMouse = false;
        return;
    }

    double dx = x - m_LastMouseX;
    double dy = y - m_LastMouseY;

    m_LastMouseX = x;
    m_LastMouseY = y;

    m_Camera.AddYawPitch(
        static_cast<float>(dx),
        static_cast<float>(-dy)
    );
}

void Stage12FluidFluxApp::OnMouseButton(int button, int action, int mods)
{
    if(m_GuiEnabled){
        ImGuiIO& io = ImGui::GetIO();
        if(io.WantCaptureMouse){
            return;
        }
    }

    if(button == GLFW_MOUSE_BUTTON_RIGHT && action == GLFW_PRESS){
        m_CameraControlEnabled = !m_CameraControlEnabled;
        m_FirstMouse = true;

        // 锁定/释放光标：进入相机控制时用 DISABLED（隐藏并锁定到中心，提供无边界的相对位移，
        // 移动到屏幕边缘也不会丢失 delta，实现"纯依据移动量转向"）；退出时恢复普通光标供 GUI 使用
        GLFWwindow* nativeWindow = GetWindow().GetNativeWindow();
        if(m_CameraControlEnabled){
            glfwSetInputMode(nativeWindow, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
            if(glfwRawMouseMotionSupported()){
                glfwSetInputMode(nativeWindow, GLFW_RAW_MOUSE_MOTION, GLFW_TRUE);
            }
        }
        else{
            glfwSetInputMode(nativeWindow, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
        }
    }
}