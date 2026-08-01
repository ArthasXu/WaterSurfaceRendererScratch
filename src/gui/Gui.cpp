#include "gui/Gui.h"

#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_vulkan.h>

#include <GLFW/glfw3.h>

#include <stdexcept>

namespace gui
{
namespace
{
void CheckVkResult(VkResult result)
{
    if(result != VK_SUCCESS){
        throw std::runtime_error("ImGui Vulkan call failed");
    }
}
}

std::vector<VkDescriptorPoolSize> GetDescriptorPoolSizes()
{
    return {
        {VK_DESCRIPTOR_TYPE_SAMPLER, 1000},
        {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1000},
        {VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1000},
        {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1000},
        {VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, 1000},
        {VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, 1000},
        {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1000},
        {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1000},
        {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1000},
        {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, 1000},
        {VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 1000}
    };
}

void Init(
    VkInstance instance,
    VkPhysicalDevice physicalDevice,
    VkDevice device,
    uint32_t queueFamily,
    VkQueue queue,
    VkDescriptorPool descriptorPool,
    uint32_t minImageCount,
    uint32_t imageCount,
    GLFWwindow* window,
    VkRenderPass renderPass
){
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();

    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.Fonts->AddFontFromFileTTF(
        "C:/Windows/Fonts/msyh.ttc",
        16.0f,
        nullptr,
        io.Fonts->GetGlyphRangesChineseFull()
    );

    ImGui::StyleColorsDark();

    ImGui_ImplGlfw_InitForVulkan(window, true);

    ImGui_ImplVulkan_InitInfo initInfo{};
    initInfo.ApiVersion = VK_API_VERSION_1_0;
    initInfo.Instance = instance;
    initInfo.PhysicalDevice = physicalDevice;
    initInfo.Device = device;
    initInfo.QueueFamily = queueFamily;
    initInfo.Queue = queue;
    initInfo.PipelineCache = VK_NULL_HANDLE;
    initInfo.DescriptorPool = descriptorPool;
    initInfo.MinImageCount = minImageCount;
    initInfo.ImageCount = imageCount;
    initInfo.Allocator = nullptr;
    initInfo.CheckVkResultFn = CheckVkResult;

    // 新版 imgui（2025/09 后）将渲染通道相关字段移入 PipelineInfoMain
    initInfo.PipelineInfoMain.RenderPass = renderPass;
    initInfo.PipelineInfoMain.Subpass = 0;
    initInfo.PipelineInfoMain.MSAASamples = VK_SAMPLE_COUNT_1_BIT;

    ImGui_ImplVulkan_Init(&initInfo);
}

void UploadFonts(VkDevice device, VkQueue queue, VkCommandPool commandPool)
{
    // 新版 imgui Vulkan 后端在首帧 NewFrame 时自动创建并上传字体纹理，
    // 无需手动调用；device / queue / commandPool 保留仅为兼容调用方
    (void)device;
    (void)queue;
    (void)commandPool;
}

void Shutdown()
{
    ImGui_ImplVulkan_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
}

void NewFrame()
{
    ImGui_ImplVulkan_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();
}

void Render(VkCommandBuffer commandBuffer)
{
    ImGui::Render();
    ImDrawData* drawData = ImGui::GetDrawData();

    if(drawData == nullptr || drawData->DisplaySize.x <= 0.0f || drawData->DisplaySize.y <= 0.0f){
        return;
    }

    ImGui_ImplVulkan_RenderDrawData(drawData, commandBuffer);
}

void SetMinImageCount(uint32_t minImageCount)
{
    ImGui_ImplVulkan_SetMinImageCount(minImageCount);
}
}
