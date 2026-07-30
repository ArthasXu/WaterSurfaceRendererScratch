#include "tools/river_field_baker/RiverFieldBakerApp.h"

#include "core/Log.h"
#include "gui/Gui.h"

#include "scene/water/river/RiverSpline.h"
#include "scene/water/river/RiverFieldBaker.h"
#include "scene/water/river/ProgressFieldBaker.h"
#include "scene/water/river/ShoreFieldBaker.h"
#include "scene/water/river/RiverFieldBundle.h"

#include <imgui.h>

#include <array>
#include <chrono>
#include <cstring>
#include <stdexcept>

void RiverFieldBakerApp::Start()
{
    // 默认控制点：与旧 CLI baker / Stage12 CreateRiverResources 保持一致
    m_ControlPoints = {
        {{-600.0f, 1200.0f}, 640.0f, 0.70f, 1.00f},
        {{-600.0f,  960.0f}, 580.0f, 0.78f, 1.00f},
        {{-610.0f,  760.0f}, 520.0f, 0.88f, 0.90f},
        {{-615.0f,  560.0f}, 360.0f, 1.00f, 0.70f},
        {{-620.0f,  340.0f}, 250.0f, 1.15f, 0.45f},
        {{-610.0f,   20.0f}, 190.0f, 1.25f, 0.25f},
        {{-540.0f, -260.0f}, 180.0f, 1.20f, 0.10f},
        {{-350.0f, -500.0f}, 174.0f, 1.12f, 0.05f},
        {{ -50.0f, -610.0f}, 170.0f, 1.05f, 0.00f},
        {{ 260.0f, -560.0f}, 168.0f, 1.00f, 0.00f},
        {{ 500.0f, -390.0f}, 170.0f, 0.98f, 0.00f},
        {{ 650.0f, -140.0f}, 166.0f, 0.96f, 0.00f},
        {{ 770.0f,  100.0f}, 162.0f, 0.94f, 0.00f},
        {{ 930.0f,  220.0f}, 158.0f, 0.92f, 0.00f}
    };

    // 场配置默认值：与运行时一致
    m_FieldConfig.worldMin = glm::vec2(-1024.0f, -1024.0f);
    m_FieldConfig.worldSize = 2048.0f;
    m_FieldConfig.resolution = 1024;
    m_FieldConfig.bankFade = 4.0f;
    m_FieldConfig.bankFadeDistance = 16.0f;

    SetupGui();
}

void RiverFieldBakerApp::Update(core::Timestep)
{
    // 无需每帧更新，纯参数编辑
}

void RiverFieldBakerApp::Render(VkCommandBuffer commandBuffer, uint32_t imageIndex)
{
    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;

    if(vkBeginCommandBuffer(commandBuffer, &beginInfo) != VK_SUCCESS){
        throw std::runtime_error("Failed to begin baker command buffer");
    }

    VkRenderPassBeginInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    renderPassInfo.renderPass = GetRenderPass();
    renderPassInfo.framebuffer = GetSwapChain().GetFramebuffer(imageIndex);
    renderPassInfo.renderArea.offset = {0, 0};
    renderPassInfo.renderArea.extent = GetSwapChain().GetExtent();

    std::array<VkClearValue, 2> clearValues{};
    clearValues[0].color.float32[0] = 0.05f;
    clearValues[0].color.float32[1] = 0.07f;
    clearValues[0].color.float32[2] = 0.10f;
    clearValues[0].color.float32[3] = 1.0f;
    clearValues[1].depthStencil = {1.0f, 0};

    renderPassInfo.clearValueCount = static_cast<uint32_t>(clearValues.size());
    renderPassInfo.pClearValues = clearValues.data();

    vkCmdBeginRenderPass(commandBuffer, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);

    DrawGui();
    gui::Render(commandBuffer);

    vkCmdEndRenderPass(commandBuffer);

    if(vkEndCommandBuffer(commandBuffer) != VK_SUCCESS){
        throw std::runtime_error("Failed to record baker command buffer");
    }
}

void RiverFieldBakerApp::SetupGui()
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

void RiverFieldBakerApp::DrawGui()
{
    gui::NewFrame();

    ImGui::SetNextWindowSize(ImVec2(480.0f, 760.0f), ImGuiCond_FirstUseEver);

    if(!ImGui::Begin("River Field Baker - 河流场离线烘焙工具", &m_GuiEnabled)){
        ImGui::End();
        return;
    }

    ImGuiIO& io = ImGui::GetIO();
    ImGui::Text("%.1f FPS", io.Framerate);
    ImGui::TextWrapped("状态: %s", m_StatusText.c_str());
    ImGui::Separator();

    // ===== 输出路径 =====
    ImGui::InputText("Output .bin - 输出烘焙文件路径", m_OutputPath, sizeof(m_OutputPath));

    // ===== 场配置 fieldConfig =====
    if(ImGui::CollapsingHeader("Field Config - 场配置：世界范围/分辨率/河岸淡出", ImGuiTreeNodeFlags_DefaultOpen)){
        ImGui::DragFloat2("World Min - 覆盖区左下角世界坐标(米)", &m_FieldConfig.worldMin.x, 8.0f);
        ImGui::DragFloat("World Size - 覆盖区边长(米,正方形)", &m_FieldConfig.worldSize, 8.0f, 128.0f, 8192.0f);

        // 分辨率：数据驱动，改后 shader 无需改，但越大烘焙越慢、显存越高
        const int resolutions[] = {256, 512, 1024, 2048};
        const char* resLabels[] = {"256", "512", "1024", "2048"};
        int resIndex = 2;
        for(int i = 0; i < 4; ++i){
            if(static_cast<int>(m_FieldConfig.resolution) == resolutions[i]){
                resIndex = i;
            }
        }
        if(ImGui::Combo("Resolution - 贴图分辨率(NxN)", &resIndex, resLabels, 4)){
            m_FieldConfig.resolution = static_cast<uint32_t>(resolutions[resIndex]);
        }

        ImGui::DragFloat("Bank Fade - 河岸淡出起始距离(米)", &m_FieldConfig.bankFade, 0.5f, 0.0f, 256.0f);
        ImGui::DragFloat("Bank Fade Dist - 河岸淡出总过渡距离(米)", &m_FieldConfig.bankFadeDistance, 0.5f, 0.0f, 512.0f);

        ImGui::TextDisabled("提示: 改 worldMin/worldSize 后，运行时四叉树 rootCenter/rootSize 需覆盖该范围");
    }

    // ===== spline 平滑 =====
    if(ImGui::CollapsingHeader("Spline - 样条平滑粒度", ImGuiTreeNodeFlags_DefaultOpen)){
        ImGui::SliderInt("Samples/Segment - 每段插值采样数", &m_SamplesPerSegment, 2, 128);
        ImGui::TextDisabled("越大曲线越平滑，烘焙耗时几乎不变（主要成本在逐像素投影）");
    }

    // ===== 岸线场参数 =====
    if(ImGui::CollapsingHeader("Shore Params - 岸线场：湿润带/沙滩/岸上地形")){
        ImGui::SliderFloat("Wet Runup - 岸上湿润带延伸(米)", &m_ShoreParams.wetRunup, 0.0f, 120.0f);
        ImGui::SliderFloat("Sand Width - 沙滩影响半径(米)", &m_ShoreParams.sandWidth, 0.0f, 200.0f);
        ImGui::SliderFloat("Beach Slope - 岸上地形坡度", &m_ShoreParams.beachSlope, 0.0f, 1.0f, "%.3f");
        ImGui::SliderFloat("Max Beach Height - 岸上地形最大抬升(米)", &m_ShoreParams.maxBeachHeight, 0.0f, 60.0f);
    }

    // ===== 控制点编辑 =====
    if(ImGui::CollapsingHeader("Control Points - 河流中轴线控制点(增删改)", ImGuiTreeNodeFlags_DefaultOpen)){
        ImGui::Text("点数: %d (至少需要 2 个)", static_cast<int>(m_ControlPoints.size()));

        int deleteIndex = -1;
        int insertIndex = -1;

        for(size_t i = 0; i < m_ControlPoints.size(); ++i){
            ImGui::PushID(static_cast<int>(i));
            water::RiverControlPoint& cp = m_ControlPoints[i];

            ImGui::Text("#%d", static_cast<int>(i));
            ImGui::DragFloat2("Position - 位置(米)", &cp.position.x, 2.0f);
            ImGui::DragFloat("Half Width - 河道半宽(米)", &cp.halfWidth, 1.0f, 1.0f, 2000.0f);
            ImGui::DragFloat("Bore Amp - 涌潮振幅缩放", &cp.boreAmplitude, 0.01f, 0.0f, 4.0f);
            ImGui::DragFloat("Curvature - 曲率权重[0..1]", &cp.curvatureWeight, 0.01f, 0.0f, 1.0f);

            if(ImGui::Button("Insert After - 在此后插入")){
                insertIndex = static_cast<int>(i);
            }
            ImGui::SameLine();
            if(ImGui::Button("Delete - 删除此点")){
                deleteIndex = static_cast<int>(i);
            }

            ImGui::Separator();
            ImGui::PopID();
        }

        if(ImGui::Button("Add Point (append) - 末尾追加控制点")){
            water::RiverControlPoint cp{};
            if(!m_ControlPoints.empty()){
                cp = m_ControlPoints.back();
                cp.position += glm::vec2(0.0f, -200.0f);
            }
            m_ControlPoints.push_back(cp);
        }

        // 在循环外执行插入/删除，避免迭代中修改容器
        if(insertIndex >= 0){
            water::RiverControlPoint cp = m_ControlPoints[insertIndex];
            m_ControlPoints.insert(m_ControlPoints.begin() + insertIndex + 1, cp);
        }
        if(deleteIndex >= 0 && m_ControlPoints.size() > 2){
            m_ControlPoints.erase(m_ControlPoints.begin() + deleteIndex);
        }
    }

    ImGui::Separator();

    if(ImGui::Button("Bake & Save - 烘焙并保存", ImVec2(-1.0f, 40.0f))){
        BakeAndSave();
    }

    ImGui::End();
}

void RiverFieldBakerApp::BakeAndSave()
{
    if(m_ControlPoints.size() < 2){
        m_StatusText = "ERROR: need at least 2 control points.";
        return;
    }

    m_StatusText = "Baking...";

    water::RiverSpline spline;
    spline.Build(m_ControlPoints, static_cast<uint32_t>(m_SamplesPerSegment));

    auto t0 = std::chrono::high_resolution_clock::now();

    water::RiverFieldData fieldData =
        water::BakeRiverField(m_FieldConfig, spline);
    water::ProgressFieldData progressData =
        water::BakeProgressField(m_FieldConfig, spline);
    water::ShoreFieldData shoreData =
        water::BakeShoreField(m_FieldConfig, spline, m_ShoreParams);

    auto t1 = std::chrono::high_resolution_clock::now();
    double seconds = std::chrono::duration<double>(t1 - t0).count();

    water::RiverFieldBundle bundle{};
    bundle.config = m_FieldConfig;
    bundle.riverLength = fieldData.riverLength;
    bundle.flow = std::move(fieldData.flow);
    bundle.coordinate = std::move(fieldData.coordinate);
    bundle.progress = std::move(progressData.field);
    bundle.shore = std::move(shoreData.field);

    if(!water::SaveRiverFieldBundle(m_OutputPath, bundle)){
        m_StatusText = std::string("ERROR: failed to write ") + m_OutputPath;
        return;
    }

    char buf[256];
    std::snprintf(
        buf, sizeof(buf),
        "Saved '%s' (res %u, %.2fs, riverLength %.1fm)",
        m_OutputPath, m_FieldConfig.resolution, seconds, bundle.riverLength
    );
    m_StatusText = buf;
    VKP_INFO("[baker] {}", m_StatusText);
}

void RiverFieldBakerApp::ShutdownApp()
{
    gui::Shutdown();

    if(m_GuiDescriptorPool != VK_NULL_HANDLE){
        vkDestroyDescriptorPool(GetDevice(), m_GuiDescriptorPool, nullptr);
        m_GuiDescriptorPool = VK_NULL_HANDLE;
    }
}
