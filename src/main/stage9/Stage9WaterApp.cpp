#include "main/stage9/Stage9WaterApp.h"

#include "core/Log.h"

#include "scene/water/render/WaterVertex.h"
#include "scene/water/bore/BoreWaveProfile.h"
#include "scene/water/foam/FoamDetailGenerator.h"

#include <GLFW/glfw3.h>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/packing.hpp>

#include <array>
#include <cmath>
#include <sstream>
#include <stdexcept>
#include <iostream>
#include <algorithm>

// Start()
// ├── VKP_INFO("Stage9WaterApp started")
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

void Stage9WaterApp::Start()
{
    VKP_INFO("Stage9WaterApp started");

    static_assert(sizeof(glm::vec4) == 16);

    m_Camera.AddYawPitch(0.0f, -300.0f);

    CreateDescriptorSetLayout();
    CreateAppearanceDescriptorSetLayout();
    CreatePipelines();
    CreateWaterGrid();
    CreateSamplers();
    CreateBoreFrontResources();
    CreateBoreProfileResources();
    CreateFoamResources();
    CreateFoamStateResources();
    CreateFoamComputeDescriptorSetLayouts();
    CreateFoamComputePipelines();
    CreateGPUFFTSource();
    CreateUniformBuffers();
    CreateDescriptorPool();
    CreateAppearanceDescriptorPool();
    CreateFoamComputeDescriptorPool();
    CreateDescriptorSets();
    CreateAppearanceDescriptorSets();
    CreateFoamComputeDescriptorSets();
    InitializeFoamStateImages();
}

void Stage9WaterApp::ShutdownApp()
{
    m_SolidPipeline.reset();
    m_WireframePipeline.reset();

    m_FoamSourcePipeline.reset();
    m_FoamAdvectPipeline.reset();

    m_DescriptorSets.clear();
    m_AppearanceDescriptorSets.clear();
    m_FoamSourceSets.clear();
    m_FoamAdvectSets = {};

    m_FoamSourceVelocityImage.reset();
    m_FoamStateImages[0].reset();
    m_FoamStateImages[1].reset();

    m_FoamDetailTexture.reset();
    m_FrontDerivativeTexture.reset();
    m_FrontParameterTexture.reset();
    m_BoreProfileDerivativeTexture.reset();
    m_BoreProfileDisplacementTexture.reset();

    m_FoamComputeDescriptorPool.reset();
    m_AppearanceDescriptorPool.reset();
    m_DescriptorPool.reset();

    m_FoamSourceSetLayout.reset();
    m_FoamAdvectSetLayout.reset();
    m_AppearanceDescriptorSetLayout.reset();
    m_DescriptorSetLayout.reset();

    m_TessendorfGPU.reset();

    m_FoamDetailSampler.reset();
    m_FFTSampler.reset();
    m_FrontLUTSampler.reset();
    m_BoreProfileSampler.reset();

    m_FoamSimulationUniformBuffers.clear();
    m_FoamParamsUniformBuffers.clear();
    m_WaterParamsUniformBuffers.clear();
    m_CameraUniformBuffers.clear();
    m_BoreFrontUniformBuffers.clear();
    m_BoreProfileUniformBuffers.clear();

    m_WaterGrid.reset();
}

void Stage9WaterApp::CreatePipelines()
{
    vkp::PipelineConfig config{};

    config.descriptorSetLayouts = {
        *m_DescriptorSetLayout,
        *m_AppearanceDescriptorSetLayout
    };

    auto bindingDescription = water::WaterVertex::GetBindingDescription();
    auto attributeDescriptions = water::WaterVertex::GetAttributeDescriptions();

    config.bindingDescriptions = {bindingDescription};
    config.attributeDescriptions = {
        attributeDescriptions[0],
        attributeDescriptions[1],
        attributeDescriptions[2]
    };

    config.depthTestEnable = true;
    config.depthWriteEnable = true;
    config.depthCompareOp = VK_COMPARE_OP_LESS;

    config.cullMode = VK_CULL_MODE_NONE;
    config.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    config.polygonMode = VK_POLYGON_MODE_FILL;

    m_SolidPipeline = std::make_unique<vkp::Pipeline>(
        GetDevice(),
        GetRenderPass(),
        "shaders/water/stage9_water.vert.spv",
        "shaders/water/stage9_water.frag.spv",
        config
    );

    if(GetDevice().SupportsFillModeNonSolid()){
        vkp::PipelineConfig wireframeConfig = config;
        wireframeConfig.polygonMode = VK_POLYGON_MODE_LINE;

        m_WireframePipeline = std::make_unique<vkp::Pipeline>(
            GetDevice(),
            GetRenderPass(),
            "shaders/water/stage9_water.vert.spv",
            "shaders/water/stage9_water.frag.spv",
            wireframeConfig
        );
    }
}

void Stage9WaterApp::CreateFoamComputePipelines()
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

// 它定义了一套接口规范：
    // 这个描述符集有多少个 binding。
    // 每个 binding 的类型是什么（UBO、纹理、存储缓冲等）。
    // 哪些着色器阶段可以访问（顶点、片段、计算）。

// ===== 几何描述符集布局（set = 0）：水面几何与物理资源 =====
// 包含相机、FFT 波浪、涌潮波前（BoreFront）和涌潮剖面（BoreProfile）的全部资源
void Stage9WaterApp::CreateDescriptorSetLayout()
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
        .Build();
}

// ===== 外观描述符集布局（set = 1）：泡沫与材质资源 =====
// 与几何/物理管线完全解耦，未来适配不同材质或 FFT 源时只需替换此 set
void Stage9WaterApp::CreateAppearanceDescriptorSetLayout()
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
            .Build();
}

// ===== 泡沫计算描述符集布局（set = 2） =====
void Stage9WaterApp::CreateFoamComputeDescriptorSetLayouts()
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
            .AddBinding(8, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_SHADER_STAGE_COMPUTE_BIT)
            .Build();

    m_FoamAdvectSetLayout =
        vkp::DescriptorSetLayout::Builder(GetDevice())
            .AddBinding(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT)
            .AddBinding(1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_COMPUTE_BIT)
            .AddBinding(2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_COMPUTE_BIT)
            .AddBinding(3, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_SHADER_STAGE_COMPUTE_BIT)
            .Build();
}

void Stage9WaterApp::CreateWaterGrid()
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

void Stage9WaterApp::CreateSamplers()
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

    m_FoamDetailSampler = std::make_unique<water::WaterSampler>(
        GetDevice(),
        VK_FILTER_LINEAR,
        VK_SAMPLER_ADDRESS_MODE_REPEAT
    );
}

void Stage9WaterApp::CreateBoreFrontResources()
{
    m_BoreFrontParams.origin = glm::vec2(0.0f);
    m_BoreFrontParams.direction = glm::normalize(glm::vec2(1.0f, 0.15f));
    m_BoreFrontParams.speed = 8.0f;
    m_BoreFrontParams.frontLength = 1000.0f;
    m_BoreFrontParams.initialOffset = -100.0f;
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

void Stage9WaterApp::CreateBoreProfileResources()
{
    m_BoreProfileConfig = water::BoreWaveProfileConfig{};
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
}

void Stage9WaterApp::CreateFoamResources()
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
            )
        );
}

void Stage9WaterApp::CreateFoamStateResources()
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

void Stage9WaterApp::CreateGPUFFTSource()
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

void Stage9WaterApp::CreateUniformBuffers()
{
    m_BoreFrontUniformBuffers.clear();
    m_BoreProfileUniformBuffers.clear();
    m_FoamParamsUniformBuffers.clear();
    m_CameraUniformBuffers.clear();
    m_WaterParamsUniformBuffers.clear();
    m_FoamSimulationUniformBuffers.clear();

    m_CameraUniformBuffers.reserve(GetMaxFramesInFlight());
    m_WaterParamsUniformBuffers.reserve(GetMaxFramesInFlight());
    m_BoreFrontUniformBuffers.reserve(GetMaxFramesInFlight());
    m_BoreProfileUniformBuffers.reserve(GetMaxFramesInFlight());
    m_FoamParamsUniformBuffers.reserve(GetMaxFramesInFlight());
    m_FoamSimulationUniformBuffers.reserve(GetMaxFramesInFlight());

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
    }
}

void Stage9WaterApp::CreateDescriptorPool()
{
    m_DescriptorPool = vkp::DescriptorPool::Builder(GetDevice())
        .SetMaxSets(GetMaxFramesInFlight())
        .AddPoolSize(
            VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
            GetMaxFramesInFlight() * 4
        )
        .AddPoolSize(
            VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            GetMaxFramesInFlight() * 10
        )
        .Build();
}

void Stage9WaterApp::CreateAppearanceDescriptorPool()
{
    m_AppearanceDescriptorPool =
        vkp::DescriptorPool::Builder(GetDevice())
            .SetMaxSets(GetMaxFramesInFlight())
            .AddPoolSize(
                VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                GetMaxFramesInFlight()
            )
            .AddPoolSize(
                VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                GetMaxFramesInFlight() * 3
            )
            .Build();
}

void Stage9WaterApp::CreateFoamComputeDescriptorPool()
{
    m_FoamComputeDescriptorPool =
        vkp::DescriptorPool::Builder(GetDevice())
            .SetMaxSets(GetMaxFramesInFlight() + 2)
            .AddPoolSize(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, GetMaxFramesInFlight() * 4 + 2)
            .AddPoolSize(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, GetMaxFramesInFlight() * 4 + 4)
            .AddPoolSize(VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, GetMaxFramesInFlight() + 2)
            .Build();
}

// 为每一个飞行帧分配描述符集，并将具体的缓冲区和纹理绑定到着色器的槽位上。
// 负责创建图形着色器（顶点/片段着色器） 使用的描述符集
// 用途：为水面渲染绑定摄像机矩阵、水体参数、位移图、法线辅助图等资源。
// 绑定内容：CameraUBO、WaterParamsUBO、fftDisplacement0~2（三层的位移纹理）、fftNormalAux0~2（三层的法线辅助纹理）等。
// 着色器类型：顶点着色器（.vert）、片段着色器（.frag）。
// 描述符集布局：m_DescriptorSetLayout（在 Stage9WaterApp 中创建，与计算管线的布局不同）。
void Stage9WaterApp::CreateDescriptorSets()
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
            .Build(m_DescriptorSets[i]); // 完成分配与写入

        // 如果分配或写入失败（比如池子空间不足），立即抛出异常
        if(!success){
            throw std::runtime_error("Failed to allocate Stage6 GPU FFT descriptor set");
        }
    }
}

void Stage9WaterApp::CreateAppearanceDescriptorSets()
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
                *m_FoamDetailSampler
            );

        VkDescriptorImageInfo foamState1Info =
            m_FoamStateImages[1]->GetSampledDescriptorInfo(
                *m_FoamDetailSampler
            );

        bool success =
            vkp::DescriptorWriter(
                *m_AppearanceDescriptorSetLayout,
                *m_AppearanceDescriptorPool
            )
                .WriteBuffer(0, &foamParamsInfo)
                .WriteImage(1, &foamDetailInfo)
                .WriteImage(2, &foamState0Info)
                .WriteImage(3, &foamState1Info)
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
void Stage9WaterApp::CreateFoamComputeDescriptorSets()
{
    m_FoamSourceSets.resize(GetMaxFramesInFlight());

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
                .WriteImage(8, &sourceVelocityStorageInfo)
                .Build(m_FoamSourceSets[i]);

        if(!success){
            throw std::runtime_error("Failed to allocate foam source descriptor set");
        }
    }

    for(uint32_t i = 0; i < 2; i++){
        uint32_t readIndex =
            i;

        uint32_t writeIndex =
            1 - i;

        VkDescriptorBufferInfo foamSimulationInfo{};
        foamSimulationInfo.buffer = *m_FoamSimulationUniformBuffers[0];
        foamSimulationInfo.offset = 0;
        foamSimulationInfo.range = sizeof(water::FoamSimulationUBO);

        VkDescriptorImageInfo previousFoamInfo =
            m_FoamStateImages[readIndex]->GetSampledDescriptorInfo(
                *m_FoamDetailSampler
            );

        VkDescriptorImageInfo sourceVelocityInfo =
            m_FoamSourceVelocityImage->GetSampledDescriptorInfo(
                *m_FoamDetailSampler
            );

        VkDescriptorImageInfo nextFoamInfo =
            m_FoamStateImages[writeIndex]->GetStorageDescriptorInfo();

        bool success =
            vkp::DescriptorWriter(
                *m_FoamAdvectSetLayout,
                *m_FoamComputeDescriptorPool
            )
                .WriteBuffer(0, &foamSimulationInfo)
                .WriteImage(1, &previousFoamInfo)
                .WriteImage(2, &sourceVelocityInfo)
                .WriteImage(3, &nextFoamInfo)
                .Build(m_FoamAdvectSets[i]);

        if(!success){
            throw std::runtime_error("Failed to allocate foam advect descriptor set");
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
void Stage9WaterApp::RecordFoamSimulation(
    VkCommandBuffer commandBuffer,
    uint32_t frameIndex
)
{
    uint32_t readIndex =
        m_CurrentFoamStateIndex;

    uint32_t writeIndex =
        1 - readIndex;

    m_FoamSourceVelocityImage->RecordFragmentReadToComputeWriteBarrier(commandBuffer);
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
        &m_FoamAdvectSets[readIndex],
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

void Stage9WaterApp::InitializeFoamStateImages()
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

void Stage9WaterApp::UpdateCamera(float deltaTime)
{
    float distance = 20.0f * deltaTime;

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

void Stage9WaterApp::Update(core::Timestep timestep)
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

    if(!m_BorePaused){
        boreDeltaTime = deltaTime;
    }

    m_LastBoreDeltaTime = boreDeltaTime;
    m_BoreTime += boreDeltaTime;

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
        m_ProfileTime = 0.0f;
    }
}

void Stage9WaterApp::PrepareFrame(uint32_t frameIndex, uint32_t imageIndex)
{
    UpdateCameraUniformBuffer(frameIndex);
    UpdateWaterParamsUniformBuffer(frameIndex);
    UpdateBoreFrontUniformBuffer(frameIndex);
    UpdateBoreProfileUniformBuffer(frameIndex);
    UpdateFoamParamsUniformBuffer(frameIndex);
    UpdateFoamSimulationUniformBuffer(frameIndex);
}

void Stage9WaterApp::UpdateCameraUniformBuffer(uint32_t frameIndex)
{
    VkExtent2D extent = GetSwapChain().GetExtent();

    float aspect =
        static_cast<float>(extent.width) /
        static_cast<float>(extent.height);

    CameraUBO ubo{};
    ubo.model = glm::mat4(1.0f);
    ubo.view = m_Camera.GetViewMatrix();
    ubo.projection = m_Camera.GetProjectionMatrix(aspect);
    ubo.debug = glm::ivec4(m_DebugMode, 0, 0, 0);

    m_CameraUniformBuffers[frameIndex]->CopyToMapped(
        &ubo,
        sizeof(ubo)
    );
}

void Stage9WaterApp::UpdateWaterParamsUniformBuffer(uint32_t frameIndex)
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

void Stage9WaterApp::UpdateBoreFrontUniformBuffer(uint32_t frameIndex)
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

void Stage9WaterApp::UpdateBoreProfileUniformBuffer(uint32_t frameIndex)
{
    water::BoreProfileUBO ubo{};

    // ===== domain：涌潮剖面的空间与时域参数 =====
    // x = profileHalfWidth（米）：剖面覆盖的半宽度，控制涌潮波形沿距离轴的影响范围
    // y = waterRiseHeight（米）：潮后水位抬升高度，模拟涨潮时整体水位上升
    // z = riseWidth（米）：水位抬升的过渡宽度，控制从无到有的平滑过渡距离
    // w = duration（秒）：单次 OneShot 动画的总时长
    ubo.domain = glm::vec4(
        m_BoreProfileConfig.profileHalfWidth,  // 剖面半宽度（如 30.0 米）
        1.5f,                                  // 潮后水位抬升高度
        8.0f,                                  // 抬升过渡宽度
        m_BoreProfileConfig.duration           // 动画时长（如 24 秒）
    );

    // ===== animation：涌潮动画控制参数 =====
    // x = profileTime（秒）：当前动画时间，驱动 Wave Profile 的 V 轴采样，控制翻卷进度
    // y = profileWidth：剖面宽度缩放因子，在着色器中用于映射 signedDistance 到纹理 U 轴
    // z = animationMode：动画模式（0 = OneShot 单次播放，1 = Looping 循环播放）
    // w = profileEnabled：剖面效果开关（1.0 = 启用涌潮，0.0 = 关闭）
    ubo.animation = glm::vec4(
        m_ProfileTime,                          // 当前动画时间 驱动 Wave Profile 翻卷动画
        0.08f,                                  // 剖面宽度缩放（典型值 0.05~0.1）
        static_cast<float>(m_ProfileMode),      // 动画模式（OneShot=0, Looping=1）
        m_ProfileEnabled ? 1.0f : 0.0f          // 效果开关
    );

    // ===== geometry：几何变换缩放因子 =====
    // x = globalAmplitude：全局振幅缩放，整体调节涌潮波高
    // y = forwardScale：前向水平位移缩放，控制推挤强度
    // z = upwardScale：向上垂直位移缩放，控制浪高
    // w = activeRegionMask：当前点是否在涌潮活跃区域内（由 Flow Map / SDF 控制）
    ubo.geometry = glm::vec4(
        1.0f,  // 全局振幅缩放（默认 1.0，调大波更高）
        3.0f,  // 前向位移缩放
        1.0f,  // 向上位移缩放
        1.0f   // 区域掩码（1.0 = 全部生效）
    );

    // ===== suppression：FFT 背景波浪抑制系数（潮头浪尖处压低 FFT 波浪） =====
    // x = shortWaveSuppression：短波（高频）抑制系数，推荐 0.2~0.4
    // y = midWaveSuppression：中波抑制系数，推荐 0.35~0.7
    // z = longWaveSuppression：长波（低频）抑制系数，推荐 0.7~1.0
    // w = 预留（未使用）
    // 原则：短波在潮头处被压得最狠，长涌浪保留最多，这样更自然
    ubo.suppression = glm::vec4(
        0.20f,  // 短波抑制（潮头处短波降至 20%）
        0.35f,  // 中波抑制（潮头处中波降至 35%）
        0.80f,  // 长波抑制（潮头处长波保留 80%）
        0.0f    // 预留
    );

    m_BoreProfileUniformBuffers[frameIndex]->CopyToMapped(
        &ubo,
        sizeof(ubo)
    );
}

void Stage9WaterApp::UpdateFoamParamsUniformBuffer(uint32_t frameIndex)
{
    water::FoamParamsUBO ubo{};

    // ==== animation：泡沫动画控制参数 =====
    ubo.animation = glm::vec4(
        m_Time,
        4.0f,   // 三相位循环周期，越小泡沫滚动越快
        0.08f,  // 世界空间纹理缩放，越大泡沫细节越密
        1.0f    // 当前没用
    );

    // ==== sourceStrength：泡沫强度控制 =====
    // 想让潮头更白：调大 x/w。
    // 想让海浪自身白沫更多：调大 y/z
    ubo.sourceStrength = glm::vec4(
        1.0f,   // Profile foam 强度，潮头/潮后泡沫
        0.35f,  // Slope foam 强度，陡坡产生泡沫
        0.45f,  // Jacobian foam 强度，FFT 破碎泡沫
        0.75f   // Bore breaking 强度，潮头破碎泡沫
    );

    // ==== thresholds：泡沫生成阈值控制 =====
    // x 越低，越容易出泡沫；y 越低，泡沫更快变满
    ubo.thresholds = glm::vec4(
        0.35f,
        0.35f,
        0.15f,
        0.65f
    );

    // ==== appearance：泡沫外观控制 =====
    ubo.appearance = glm::vec4(
        0.32f,  // x = coverageThreshold：越低，泡沫覆盖越多
        0.15f,  // y = coverageSoftness：越大，边缘越软
        0.35f,  // z = foamNormalStrength：目前未接入光照
        0.0f    // w = stateFoamBlend：状态泡沫未做，暂时无效
    );

    ubo.state = glm::vec4(
        1.8f,
        0.45f,
        0.02f,
        0.0f
    );

    ubo.runtime = glm::vec4(
        static_cast<float>(m_CurrentFoamStateIndex),
        1.0f,
        0.0f,
        0.0f
    );

    m_FoamParamsUniformBuffers[frameIndex]->CopyToMapped(
        &ubo,
        sizeof(ubo)
    );
}

void Stage9WaterApp::UpdateFoamSimulationUniformBuffer(uint32_t frameIndex)
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
    ubo.domain = glm::vec4(
        -128.0f,   // worldMinX
        -128.0f,   // worldMinZ
        256.0f,    // worldSizeX
        256.0f     // worldSizeZ
    );

    // ===== simulation：时变模拟参数 =====
    // x: foamDt    – 当前帧的时间步长（已限制）
    // y: m_Time    – 当前累积时间
    // z: 1.8f      – 源项增益（与 FoamParamsUBO.state.x 保持一致）
    // w: 0.45f     – 衰减系数（与 FoamParamsUBO.state.y 保持一致）
    ubo.simulation = glm::vec4(
        foamDt,
        m_Time,
        1.8f,
        0.45f
    );

    // ===== solver：数值求解参数 =====
    // x: 0.02f – 扩散系数（拉普拉斯项的权重）
    // y: m_FoamResolution      – 泡沫状态图的分辨率（如 512）
    // z: 1.0 / m_FoamResolution – 单个纹素的大小（逆分辨率），用于计算相邻像素 UV 偏移
    // w: 1.0f  – 求解器开关（1 启用，0 跳过）
    ubo.solver = glm::vec4(
        0.02f,                                          // diffusion
        static_cast<float>(m_FoamResolution),           // resolution
        1.0f / static_cast<float>(m_FoamResolution),    // invResolution
        1.0f                                             // enabled
    );

    // 将数据写入当前帧对应的 Uniform Buffer（持久映射，直接拷贝）
    m_FoamSimulationUniformBuffers[frameIndex]->CopyToMapped(
        &ubo,
        sizeof(ubo)
    );
}
void Stage9WaterApp::Render(VkCommandBuffer commandBuffer, uint32_t imageIndex)
{
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

    m_WaterGrid->Bind(commandBuffer);
    m_WaterGrid->Draw(commandBuffer);

    vkCmdEndRenderPass(commandBuffer);

    if(vkEndCommandBuffer(commandBuffer) != VK_SUCCESS){
        throw std::runtime_error("Failed to record Stage6 command buffer");
    }
}

void Stage9WaterApp::UpdateWindowTitle()
{
    m_TitleUpdateTimer += 1.0f / 60.0f;

    if(m_TitleUpdateTimer < 0.5f){
        return;
    }

    m_TitleUpdateTimer = 0.0f;

    const glm::vec3& pos = m_Camera.GetPosition();

    std::ostringstream title;
    title << "Stage 9 - Water Foam | pos=("
        << pos.x << ", "
        << pos.y << ", "
        << pos.z << ") mode="
        << m_DebugMode
        << " wire="
        << (m_UseWireframe ? "on" : "off")
        << " paused="
        << (m_Paused ? "yes" : "no");

    GetWindow().SetTitle(title.str());
}

void Stage9WaterApp::OnFramebufferResize(int width, int height)
{
    core::Application::OnFramebufferResize(width, height);
}

void Stage9WaterApp::OnKey(int key, int scancode, int action, int mods)
{
    // 保留基类的默认按键行为（如 ESC 退出等）
    core::Application::OnKey(key, scancode, action, mods);

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
            m_DebugMode = (m_DebugMode + 1) % 40;
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
            m_ProfileTime = 0.0f;
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

void Stage9WaterApp::OnMouseMove(double x, double y)
{
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

void Stage9WaterApp::OnMouseButton(int button, int action, int mods)
{
    if(button == GLFW_MOUSE_BUTTON_RIGHT && action == GLFW_PRESS){
        m_CameraControlEnabled = !m_CameraControlEnabled;
        m_FirstMouse = true;
    }
}