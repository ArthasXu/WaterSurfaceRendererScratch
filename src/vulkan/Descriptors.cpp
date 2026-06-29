#include "vulkan/Descriptors.h"

#include <cassert>
#include <stdexcept>

namespace vkp
{
// DescriptorSetLayout 是对 Vulkan 描述符集布局对象的封装，它的核心作用是定义着色器中资源绑定的“接口规范”。
// 具体来说，它描述了一个描述符集内部包含哪些 binding，每个 binding 是什么类型（Uniform Buffer、采样器、存储缓冲等），以及它们对应的着色器阶段（顶点、片段、计算等）    
DescriptorSetLayout::Builder::Builder(VkDevice device)
    : m_Device(device)
{
}

DescriptorSetLayout::Builder& DescriptorSetLayout::Builder::AddBinding(
    uint32_t binding,
    VkDescriptorType type,
    VkShaderStageFlags stageFlags,
    uint32_t count
){
    assert(m_Bindings.count(binding) == 0 && "Binding already in use");

    VkDescriptorSetLayoutBinding layoutBinding{}; // 描述符集布局绑定
    layoutBinding.binding = binding; // 绑定点
    layoutBinding.descriptorType = type; // 描述符类型
    layoutBinding.descriptorCount = count; // 描述符数量
    layoutBinding.stageFlags = stageFlags; // 着色器阶段
    layoutBinding.pImmutableSamplers = nullptr; // 不可变采样器

    m_Bindings[binding] = layoutBinding; // 添加到绑定列表
    return *this; // 返回自身
}

std::unique_ptr<DescriptorSetLayout> DescriptorSetLayout::Builder::Build() const{ // 构建描述符集布局
    return std::make_unique<DescriptorSetLayout>(m_Device, m_Bindings); // 返回描述符集布局
}

DescriptorSetLayout::DescriptorSetLayout(
    VkDevice device,
    std::unordered_map<uint32_t, VkDescriptorSetLayoutBinding> bindings
)
    : m_Device(device), m_Bindings(std::move(bindings)){ // 构造函数
    std::vector<VkDescriptorSetLayoutBinding> setLayoutBindings{}; // 描述符集布局绑定列表
    setLayoutBindings.reserve(m_Bindings.size()); // 预留空间

    for(const auto& binding : m_Bindings){
        setLayoutBindings.push_back(binding.second); // 添加到描述符集布局绑定列表
    }

    VkDescriptorSetLayoutCreateInfo layoutInfo{}; // 描述符集布局创建信息
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO; // 结构体类型
    layoutInfo.bindingCount = static_cast<uint32_t>(setLayoutBindings.size()); // 绑定数量
    layoutInfo.pBindings = setLayoutBindings.data(); // 绑定列表

    if(vkCreateDescriptorSetLayout(m_Device, &layoutInfo, nullptr, &m_DescriptorSetLayout) != VK_SUCCESS){ // 创建描述符集布局
        throw std::runtime_error("Failed to create descriptor set layout!");
    }
}

DescriptorSetLayout::~DescriptorSetLayout(){ // 析构函数
    if(m_DescriptorSetLayout != VK_NULL_HANDLE){ // 描述符集布局句柄不为空
        vkDestroyDescriptorSetLayout(m_Device, m_DescriptorSetLayout, nullptr); // 销毁描述符集布局
    }    
}

DescriptorSetLayout::operator VkDescriptorSetLayout() const
{
    return m_DescriptorSetLayout;
}

VkDevice DescriptorSetLayout::GetDevice() const
{
    return m_Device;
}

const std::unordered_map<uint32_t, VkDescriptorSetLayoutBinding>& DescriptorSetLayout::GetBindings() const
{
    return m_Bindings;
}



// DescriptorPool 的作用可以概括为：它是创建 DescriptorSet 的“内存池/工厂”。没有它，你就无法从 DescriptorSetLayout（规格说明）实例化出实际可用的 DescriptorSet（插头）。
// DescriptorSetLayout 定义了“插座规格”——比如需要几个 binding，每个是什么类型（UBO、纹理采样器等）。
// DescriptorSet 是具体的“插头”——内部已经连接好具体的 Buffer 或纹理，可以直接绑定给着色器。
// DescriptorPool 则是创建这些“插头”的工厂：你必须先创建一个 Pool，然后调用 vkAllocateDescriptorSets 从 Pool 里分配出符合 Layout 要求的 DescriptorSet
DescriptorPool::Builder::Builder(VkDevice device)
    : m_Device(device)
{
}

DescriptorPool::Builder& DescriptorPool::Builder::AddPoolSize(VkDescriptorType type, uint32_t count){ // 添加一个描述符池大小
    m_PoolSizes.push_back({type, count});
    return *this;
}

DescriptorPool::Builder& DescriptorPool::Builder::SetMaxSets(uint32_t count){ // 设置最大描述符集数量
    m_MaxSets = count;
    return *this;
}

std::unique_ptr<DescriptorPool> DescriptorPool::Builder::Build() const{ // 构建描述符池
    return std::make_unique<DescriptorPool>(m_Device, m_MaxSets, m_PoolSizes);
}

DescriptorPool::DescriptorPool(
    VkDevice device,
    uint32_t maxSets,
    const std::vector<VkDescriptorPoolSize>& poolSizes
)
    : m_Device(device)
{
    VkDescriptorPoolCreateInfo poolInfo{}; // 描述符池创建信息
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO; // 结构体类型
    poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size()); // 描述符池大小数量
    poolInfo.pPoolSizes = poolSizes.data(); // 描述符池大小数组
    poolInfo.maxSets = maxSets; // 最大描述符集数量

    if(vkCreateDescriptorPool(m_Device, &poolInfo, nullptr, &m_DescriptorPool) != VK_SUCCESS){ // 创建描述符池
        throw std::runtime_error("Failed to create descriptor pool!");
    }
}

DescriptorPool::~DescriptorPool(){ // 析构函数
    if(m_DescriptorPool != VK_NULL_HANDLE){ // 描述符池句柄不为空
        vkDestroyDescriptorPool(m_Device, m_DescriptorPool, nullptr); // 销毁描述符池
    }
}

bool DescriptorPool::AllocateDescriptorSet(VkDescriptorSetLayout layout, VkDescriptorSet& set) const
{ // 分配描述符集
    VkDescriptorSetAllocateInfo allocInfo{}; // 描述符集分配信息
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO; // 结构体类型
    allocInfo.descriptorPool = m_DescriptorPool; // 描述符池
    allocInfo.descriptorSetCount = 1; // 描述符集数量
    allocInfo.pSetLayouts = &layout; // 描述符集布局

    return vkAllocateDescriptorSets(m_Device, &allocInfo, &set) == VK_SUCCESS; // 分配描述符集
}

void DescriptorPool::ResetPool(){ // 重置描述符池
    vkResetDescriptorPool(m_Device, m_DescriptorPool, 0); // 重置描述符池
}

// DescriptorWriter 的作用是简化描述符集的创建和更新过程。
// 它允许你以链式调用的方式设置描述符集的各个绑定，然后一次性构建或更新整个描述符集。
DescriptorWriter::DescriptorWriter(DescriptorSetLayout& setLayout, DescriptorPool& pool)
    : m_SetLayout(setLayout), m_Pool(pool) {}

DescriptorWriter& DescriptorWriter::WriteBuffer(uint32_t binding, VkDescriptorBufferInfo* bufferInfo)
{ // 写入一个 buffer 到这个描述符集中
    assert(m_SetLayout.GetBindings().count(binding) == 1 && "Layout does not contain binding");

    const auto& bindingDescription = m_SetLayout.GetBindings().at(binding);

    assert(bindingDescription.descriptorCount == 1 && "Binding single descriptor writes only");

    VkWriteDescriptorSet write{};
    write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.descriptorType = bindingDescription.descriptorType;
    write.dstBinding = binding;
    write.pBufferInfo = bufferInfo;
    write.descriptorCount = 1;

    m_Writes.push_back(write);
    return *this;
}

bool DescriptorWriter::Build(VkDescriptorSet& set)
{ // 构建描述符集
    bool success = m_Pool.AllocateDescriptorSet(m_SetLayout, set);

    if(!success){
        return false;
    }

    Overwrite(set);
    return true;
}

void DescriptorWriter::Overwrite(VkDescriptorSet& set)
{ // 更新描述符集
    for(auto& write : m_Writes){
        write.dstSet = set;
    }

    vkUpdateDescriptorSets(
        m_SetLayout.GetDevice(),
        static_cast<uint32_t>(m_Writes.size()),
        m_Writes.data(),
        0,
        nullptr
    );
}

}