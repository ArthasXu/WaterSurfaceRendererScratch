#pragma once

#include "scene/water/common/FFTResourceContract.h"

#include <vulkan/vulkan.h>

#include <array>
#include <cstdint>

namespace water
{
struct WaterCascadeGPUResource
{
    VkDescriptorImageInfo displacement{}; // 描述符信息
    VkDescriptorImageInfo normalAux{}; // 描述符信息

    float patchLength = 0.0f; // 面片长度
    uint32_t resolution = 0; // 分辨率
    float amplitudeScale = 1.0f; // 振幅缩放
};

struct WaterSurfaceGPUResources
{
    // array 大小在编译时确定，是静态数组；vector 大小在运行时确定，是动态数组
    // array 的元素内嵌在对象内部，不额外分配堆内存，内存也是连续的；避免堆分配开销，适合小集合
    // vector 内存分配在堆上 对象本身只存指针、大小和容量。内存也是连续的，但数据在堆上
    // vector 扩容时会重新分配内存并拷贝元素
    // push_back 需要你先构造好一个对象（或提供一个临时对象），然后将其拷贝或移动到 vector 的内存中;适合小对象，或希望明确拷贝或者移动（左值默认拷贝、右值默认移动）
    // emplace_back 直接在 vector 的内存中构造对象，避免了拷贝或移动的开销；适合大对象，或希望就地构造，或explicit构造函数
    std::array<WaterCascadeGPUResource, kMaxFFTCascades> cascades{}; // FFT级联数组

    uint32_t cascadeCount = 0; // 级联数量
};

class IGPUWaterSurfaceSource
{
public:
    virtual ~IGPUWaterSurfaceSource() = default;

    virtual void UpdateGPU(
        VkCommandBuffer commandBuffer,
        float deltaTime
    ) = 0; // 更新GPU水面高度

    virtual const WaterSurfaceGPUResources& GetGPUResources() const = 0; // 获取GPU资源
};
}