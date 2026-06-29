#pragma once

#include <vulkan/vulkan.h>

#include <memory>
#include <unordered_map>
#include <vector>

namespace vkp
{
class DescriptorSetLayout { // 描述符集布局 把具体的 VkBuffer 和这片内存绑定到这个 binding 上
public:
    class Builder
    { // 描述符集布局构建器
    public:
        Builder(VkDevice device);

        Builder& AddBinding(
            uint32_t binding,
            VkDescriptorType type,
            VkShaderStageFlags stageFlags,
            uint32_t count = 1
        ); // 添加一个 binding 到这个描述符集布局中

        std::unique_ptr<DescriptorSetLayout> Build() const; // 构建描述符集布局

    private:
        VkDevice m_Device = VK_NULL_HANDLE; // 逻辑设备句柄
        std::unordered_map<uint32_t, VkDescriptorSetLayoutBinding> m_Bindings; // 描述符集布局绑定
    };

    DescriptorSetLayout(
        VkDevice device,
        std::unordered_map<uint32_t, VkDescriptorSetLayoutBinding> bindings
    ); // 构造函数
    ~DescriptorSetLayout();

    DescriptorSetLayout(const DescriptorSetLayout&) = delete;
    DescriptorSetLayout& operator=(const DescriptorSetLayout&) = delete;

    operator VkDescriptorSetLayout() const;

    VkDevice GetDevice() const; // 获取逻辑设备句柄
    const std::unordered_map<uint32_t, VkDescriptorSetLayoutBinding>& GetBindings() const; // 获取描述符集布局绑定

private:
    VkDevice m_Device = VK_NULL_HANDLE; // 逻辑设备句柄
    VkDescriptorSetLayout m_DescriptorSetLayout = VK_NULL_HANDLE; // 描述符集布局句柄
    std::unordered_map<uint32_t, VkDescriptorSetLayoutBinding> m_Bindings; // 描述符集布局绑定
};

class DescriptorPool { // 描述符池 用来分配描述符集
public:
    class Builder
    { // 描述符池构建器
    public:
        Builder(VkDevice device);

        Builder& AddPoolSize(VkDescriptorType type, uint32_t count); // 添加一个描述符池大小
        Builder& SetMaxSets(uint32_t count); // 设置最大描述符集数量

        std::unique_ptr<DescriptorPool> Build() const;

    private:
        VkDevice m_Device = VK_NULL_HANDLE; // 逻辑设备句柄
        std::vector<VkDescriptorPoolSize> m_PoolSizes; // 描述符池大小
        uint32_t m_MaxSets = 1000; // 最大描述符集数量
    };

    DescriptorPool(
        VkDevice device,
        uint32_t maxSets,
        const std::vector<VkDescriptorPoolSize>& poolSizes
    );
    ~DescriptorPool();

    DescriptorPool(const DescriptorPool&) = delete;
    DescriptorPool& operator=(const DescriptorPool&) = delete;

    bool AllocateDescriptorSet(VkDescriptorSetLayout layout, VkDescriptorSet& set) const;
    void ResetPool();

private:
    VkDevice m_Device = VK_NULL_HANDLE;
    VkDescriptorPool m_DescriptorPool = VK_NULL_HANDLE;
};

class DescriptorWriter
{ // 描述符写入器
public:
    DescriptorWriter(DescriptorSetLayout& setLayout, DescriptorPool& pool); // 构造函数

    DescriptorWriter& WriteBuffer(uint32_t binding, VkDescriptorBufferInfo* bufferInfo); // 写入一个 buffer 到这个描述符集中

    bool Build(VkDescriptorSet& set);
    void Overwrite(VkDescriptorSet& set);

private:
    DescriptorSetLayout& m_SetLayout;
    DescriptorPool& m_Pool;
    std::vector<VkWriteDescriptorSet> m_Writes;
};
}