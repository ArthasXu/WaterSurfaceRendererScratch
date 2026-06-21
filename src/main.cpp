#include <vulkan/vulkan.h>
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>

#include <cstring>
#include <iostream>
#include <vector>

int main(){
    std::cout << "Stage 0 environment check\n";

    if (!glfwInit())
    {
        std::cerr << "Failed to initialize GLFW\n";
        return 1;
    }

    std::cout << "GLFW: " << glfwGetVersionString() << "\n";

    glm::vec3 a(1.0f, 2.0f, 3.0f);
    std::cout << "GLM sanity check: " << a.x + a.y + a.z << "\n";

    // VkResult vkEnumerateInstanceExtensionProperties(
    //     const char*   pLayerName,            // 层名称，或 nullptr
    //     uint32_t*     pPropertyCount,        // 输出：扩展数量；输入：若pProperties不为null则表示数组长度
    //     VkExtensionProperties* pProperties   // 输出：扩展属性数组，或 nullptr
    // );
    // VkExtensionProperties(
    //     char extensionName[VK_MAX_EXTENSION_NAME_SIZE],  //扩展名称（字符串）。
    //     uint32_t specVersion                             //该扩展所遵循的规范版本号。
    // );
    uint32_t extensionCount = 0;
    VkResult result = vkEnumerateInstanceExtensionProperties(nullptr, &extensionCount, nullptr);
    if (result != VK_SUCCESS)
    {
        std::cerr << "Failed to enumerate Vulkan instance extensions\n";
        glfwTerminate();
        return 1;
    }

    std::vector<VkExtensionProperties> extensions(extensionCount);
    vkEnumerateInstanceExtensionProperties(nullptr, &extensionCount, extensions.data());
    std::cout << "Vulkan instance extensions: " << extensionCount << "\n";
    for (const auto& extension : extensions)
    {
        std::cout << "  " << extension.extensionName << "\n";
    }

    // VkResult vkEnumerateInstanceLayerProperties(
    //     uint32_t*          pPropertyCount,    // 输出：层数量
    //     VkLayerProperties* pProperties        // 层属性数组，或 nullptr
    // );
    // typedef struct VkLayerProperties {
    //     char        layerName[VK_MAX_EXTENSION_NAME_SIZE];    // 层名称
    //     uint32_t    specVersion;                              // 该层所遵循的 Vulkan 规范版本
    //     uint32_t    implementationVersion;                    // 该层自身的版本
    //     char        description[VK_MAX_DESCRIPTION_SIZE];     // 层的简要描述
    // } VkLayerProperties;
    uint32_t layerCount = 0;
    vkEnumerateInstanceLayerProperties(&layerCount, nullptr);
    std::vector<VkLayerProperties> layers(layerCount);
    vkEnumerateInstanceLayerProperties(&layerCount, layers.data());

    bool hasValidationLayer = false;
    std::cout << "Vulkan instance layers: " << layerCount << "\n";
    for(const auto& layer:layers){
        std::cout << "  " << layer.layerName << "\n";
        if(std::strcmp(layer.layerName, "VK_LAYER_KHRONOS_validation") == 0){
            hasValidationLayer = true;
        }
    }
    std::cout << "VK_LAYER_KHRONOS_validation: " << (hasValidationLayer ? "FOUND" : "MISSING") << "\n";

    // typedef struct VkApplicationInfo {
    //     VkStructureType    sType;                // 必须为 VK_STRUCTURE_TYPE_APPLICATION_INFO
    //     const void*        pNext;                // 扩展指针，常为 nullptr
    //     const char*        pApplicationName;     // 应用程序名称
    //     uint32_t           applicationVersion;   // 应用程序版本
    //     const char*        pEngineName;          // 引擎名称
    //     uint32_t           engineVersion;        // 引擎版本
    //     uint32_t           apiVersion;           // 所使用的 Vulkan API 版本
    // } VkApplicationInfo;
    VkApplicationInfo appInfo{};
    appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appInfo.pApplicationName = "Stage0EnvironmentCheck";
    appInfo.applicationVersion = VK_MAKE_VERSION(0,0,1);
    appInfo.pEngineName = "ScratchRenderer";
    appInfo.engineVersion = VK_MAKE_VERSION(0,0,1);
    appInfo.apiVersion = VK_API_VERSION_1_3;

    const char* validationLayerName = "VK_LAYER_KHRONOS_validation";

    // typedef struct VkInstanceCreateInfo {
    //     VkStructureType             sType;                  // 必须为 VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO
    //     const void*                 pNext;                  // 扩展链指针，常为 nullptr
    //     VkInstanceCreateFlags       flags;                  // 保留字段，通常为 0
    //     const VkApplicationInfo*    pApplicationInfo;       // 指向 VkApplicationInfo 的指针
    //     uint32_t                    enabledLayerCount;      // 启用的实例层数量
    //     const char* const*          ppEnabledLayerNames;    // 启用的实例层名称数组
    //     uint32_t                    enabledExtensionCount;  // 启用的实例扩展数量
    //     const char* const*          ppEnabledExtensionNames;// 启用的实例扩展名称数组
    // } VkInstanceCreateInfo;
    // VkResult vkCreateInstance(
    //     const VkInstanceCreateInfo* pCreateInfo,    // 实例参数
    //     const VkAllocationCallbacks* pAllocator,    // 自定义内存分配器，常为 nullptr
    //     VkInstance* pInstance                       // 输出：实例句柄
    // );
    VkInstancCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    createInfo.pApplicationInfo = &appInfo;
    if(hasValidationLayer){
        createInfo.enabledLayerCount = 1;
        createInfo.ppEnabledExtensionNames = &validationLayerName;
    }
    VkInstance instance = VK_NULL_HANDLE;
    result = vkCreateInstance(&createInfo, nullptr, &instance);
    if(result != VK_SUCCESS){
        std::cerr << "vkCreateInstance failed: " << result << "\n";
        glfwTerminate();
        return 1;
    }

    std::cout << "vkCreateInstance: OK\n";

    // void vkDestroyInstance(
    //     VkInstance                   instance,      // 要销毁的实例句柄
    //     const VkAllocationCallbacks* pAllocator     // 自定义内存释放回调，通常为 nullptr
    // );
    vkDestroyInstance(instance, nullptr);
    glfwTerminate();

    std::cout << "Stage 0 check passed\n";
    return 0;
}