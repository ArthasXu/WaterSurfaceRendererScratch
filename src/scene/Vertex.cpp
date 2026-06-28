#include "scene/Vertex.h"

#include <cstddef>

VkVertexInputBindingDescription Vertex::GetBindingDescription()
{
    // typedef struct VkVertexInputBindingDescription {
    //     uint32_t             binding;
    //     uint32_t             stride;
    //     VkVertexInputRate    inputRate;
    // } VkVertexInputBindingDescription;
    VkVertexInputBindingDescription bindingDescription{}; // 顶点输入绑定描述，每个元素描述一个顶点缓冲区绑定
    bindingDescription.binding = 0; // 绑定点，与顶点输入属性描述中的 binding 一致
    bindingDescription.stride = sizeof(Vertex); // 顶点数据的步长，每个顶点的数据大小
    bindingDescription.inputRate = VK_VERTEX_INPUT_RATE_VERTEX; // 顶点输入速率, 每个顶点一次
    return bindingDescription; // 返回顶点输入绑定描述
}

std::array<VkVertexInputAttributeDescription, 3> Vertex::GetAttributeDescriptions()
{
    // typedef struct VkVertexInputAttributeDescription {
    //     uint32_t    location;
    //     uint32_t    binding;
    //     VkFormat    format;
    //     uint32_t    offset;
    // } VkVertexInputAttributeDescription;
    // array 大小在编译时确定，是静态数组；vector 大小在运行时确定，是动态数组
    // array 的元素内嵌在对象内部，不额外分配堆内存，内存也是连续的；避免堆分配开销，适合小集合
    // vector 内存分配在堆上 对象本身只存指针、大小和容量。内存也是连续的，但数据在堆上
    // vector 扩容时会重新分配内存并拷贝元素
    // push_back 需要你先构造好一个对象（或提供一个临时对象），然后将其拷贝或移动到 vector 的内存中;适合小对象，或希望明确拷贝或者移动（左值默认拷贝、右值默认移动）
    // emplace_back 直接在 vector 的内存中构造对象，避免了拷贝或移动的开销；适合大对象，或希望就地构造，或explicit构造函数
    std::array<VkVertexInputAttributeDescription, 3> attributeDescriptions{}; // 顶点输入属性描述

    attributeDescriptions[0].binding = 0; // 绑定点，与顶点输入绑定描述中的 binding 一致
    attributeDescriptions[0].location = 0; // 着色器中的位置，与顶点着色器中的 layout(location = 0) in vec3 position; 一致
    attributeDescriptions[0].format = VK_FORMAT_R32G32B32_SFLOAT; // 顶点数据的格式
    attributeDescriptions[0].offset = offsetof(Vertex, position); // 顶点数据的偏移量

    attributeDescriptions[1].binding = 0;
    attributeDescriptions[1].location = 1;
    attributeDescriptions[1].format = VK_FORMAT_R32G32B32_SFLOAT;
    attributeDescriptions[1].offset = offsetof(Vertex, color);

    attributeDescriptions[2].binding = 0;
    attributeDescriptions[2].location = 2;
    attributeDescriptions[2].format = VK_FORMAT_R32G32_SFLOAT;
    attributeDescriptions[2].offset = offsetof(Vertex, uv);

    return attributeDescriptions;
}