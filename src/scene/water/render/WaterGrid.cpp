#include "scene/water/render/WaterGrid.h"

#include <stdexcept>

namespace water
{
WaterGrid::WaterGrid(
    VkPhysicalDevice physicalDevice,
    VkDevice device,
    vkp::CommandPool& commandPool,
    VkQueue graphicsQueue,
    const WaterGridConfig& config,
    WaterGridUploadMode uploadMode
)
    : m_Config(config),
      m_UploadMode(uploadMode)
{
    GenerateGrid();
    CreateVertexBuffer(physicalDevice, device, commandPool, graphicsQueue);
    CreateIndexBuffer(physicalDevice, device, commandPool, graphicsQueue);
}

void WaterGrid::GenerateGrid()
{   // 生成水网格的顶点和索引数据
    const uint32_t vertexCountX = m_Config.cellCountX + 1;
    const uint32_t vertexCountZ = m_Config.cellCountZ + 1;

    m_BaseVertices.clear();
    m_BaseVertices.reserve(vertexCountX * vertexCountZ);

    // 生成顶点数据
    for(uint32_t z = 0; z < vertexCountZ; z++){
        for(uint32_t x = 0; x < vertexCountX; x++){
            float u = static_cast<float>(x) / static_cast<float>(m_Config.cellCountX);
            float v = static_cast<float>(z) / static_cast<float>(m_Config.cellCountZ);

            float px = (u - 0.5f) * m_Config.sizeX;
            float pz = (v - 0.5f) * m_Config.sizeZ;

            WaterVertex vertex{};
            vertex.position = m_Config.origin + glm::vec3(px, 0.0f, pz);
            vertex.normal = glm::vec3(0.0f, 1.0f, 0.0f);
            vertex.uv = glm::vec2(u, v);

            m_BaseVertices.push_back(vertex);
        }
    }

    m_Indices.clear();
    m_Indices.reserve(m_Config.cellCountX * m_Config.cellCountZ * 6);

    // 生成索引数据
    for(uint32_t z = 0; z < m_Config.cellCountZ; z++){
        for(uint32_t x = 0; x < m_Config.cellCountX; x++){
            uint32_t i0 = z * vertexCountX + x;
            uint32_t i1 = z * vertexCountX + x + 1;
            uint32_t i2 = (z + 1) * vertexCountX + x;
            uint32_t i3 = (z + 1) * vertexCountX + x + 1;

            m_Indices.push_back(i0);
            m_Indices.push_back(i2);
            m_Indices.push_back(i1);

            m_Indices.push_back(i1);
            m_Indices.push_back(i2);
            m_Indices.push_back(i3);
        }
    }

    m_IndexCount = static_cast<uint32_t>(m_Indices.size());
}

void WaterGrid::CreateVertexBuffer(
    VkPhysicalDevice physicalDevice,
    VkDevice device,
    vkp::CommandPool& commandPool,
    VkQueue graphicsQueue
)
{   // 创建顶点缓冲区
    VkDeviceSize bufferSize = sizeof(WaterVertex) * m_BaseVertices.size(); // 顶点数据大小

    if(m_UploadMode == WaterGridUploadMode::DynamicHostVisible){
        // 顶点数据在CPU上生成，直接映射到GPU内存中
        // GPU 渲染时从系统内存读取顶点，虽然带宽低于显存，但对于频繁更新的动态数据，这种方式延迟更低、更灵活
        m_VertexBuffer = std::make_unique<vkp::Buffer>(
            physicalDevice,
            device,
            bufferSize,
            VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
        );

        m_VertexBuffer->Map(); // 把这块 GPU 内存映射到 CPU 可访问的地址空间
        m_VertexBuffer->CopyToMapped(m_BaseVertices.data(), bufferSize); // 把顶点数据从 CPU 搬进了 host-visible 内存
        // HOST_VISIBLE 持久映射 水面顶点位置每帧变化（波浪计算），CPU 需要频繁更新
        // 到了 Stage 6，这种方式会被替换为“静态基础网格 + 在着色器里采样位移图来实现波浪变形”
        // 把重复的、并行度高的计算从 CPU 搬到 GPU，把动态更新的缓冲变成静态缓冲加动态纹理，从而提升性能
        return;
    }

    // 创建一块 CPU 可写的 stagingBuffer，将顶点数据拷入
    vkp::Buffer stagingBuffer(
        physicalDevice,
        device,
        bufferSize,
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
    );

    stagingBuffer.Map();
    stagingBuffer.CopyToMapped(m_BaseVertices.data(), bufferSize);
    stagingBuffer.Unmap();

    // 创建一块 DEVICE_LOCAL 的 m_VertexBuffer，这是真正的显存
    m_VertexBuffer = std::make_unique<vkp::Buffer>(
        physicalDevice,
        device,
        bufferSize,
        VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT
    );
    
    // 通过 commandPool.CopyBuffer 发送一条 GPU 拷贝命令，将数据从 staging 搬运到显存
    // 函数返回后，顶点数据永久驻留在显存中，GPU 读取时带宽最高、延迟最低，适合不再频繁修改的静态网格
    commandPool.CopyBuffer(
        device,
        graphicsQueue,
        stagingBuffer,
        *m_VertexBuffer,
        bufferSize
    );
}

void WaterGrid::CreateIndexBuffer(
    VkPhysicalDevice physicalDevice,
    VkDevice device,
    vkp::CommandPool& commandPool,
    VkQueue graphicsQueue
){  // 创建索引缓冲区
    VkDeviceSize bufferSize = sizeof(uint32_t) * m_Indices.size();

    vkp::Buffer stagingBuffer(
        physicalDevice,
        device,
        bufferSize,
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
    ); // 创建 staging buffer

    stagingBuffer.Map(); // 映射内存
    stagingBuffer.CopyToMapped(m_Indices.data(), bufferSize); // 把 m_Indices 里的索引数据拷贝到 staging buffer
    stagingBuffer.Unmap(); // 取消映射

    m_IndexBuffer = std::make_unique<vkp::Buffer>(
        physicalDevice,
        device,
        bufferSize,
        VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT
    ); // 创建 index buffer 这块内存在显存中，GPU 读取速度最快，但 CPU 不能直接写

    commandPool.CopyBuffer(
        device,
        graphicsQueue,
        stagingBuffer,
        *m_IndexBuffer,
        bufferSize
    ); // 拷贝 staging buffer 到 index buffer
    // staging → DEVICE_LOCAL 索引数据静态不变，一次性上传后只读，GPU 高速访问
}

void WaterGrid::Bind(VkCommandBuffer commandBuffer) const
{
    VkBuffer vertexBuffers[] = {*m_VertexBuffer};
    VkDeviceSize offsets[] = {0};

    vkCmdBindVertexBuffers(commandBuffer, 0, 1, vertexBuffers, offsets);
    vkCmdBindIndexBuffer(commandBuffer, *m_IndexBuffer, 0, VK_INDEX_TYPE_UINT32);
}

void WaterGrid::Draw(VkCommandBuffer commandBuffer) const
{
    vkCmdDrawIndexed(commandBuffer, m_IndexCount, 1, 0, 0, 0);
}

const std::vector<WaterVertex>& WaterGrid::GetBaseVertices() const
{
    return m_BaseVertices;
}

void WaterGrid::UpdateVertices(const std::vector<WaterVertex>& vertices)
{
    if(m_UploadMode != WaterGridUploadMode::DynamicHostVisible){
        throw std::runtime_error("WaterGrid::UpdateVertices is only valid for DynamicHostVisible mode");
    }
    if(vertices.size() != m_BaseVertices.size()){
        throw std::runtime_error("WaterGrid::UpdateVertices vertex count mismatch");
    }

    VkDeviceSize bufferSize = sizeof(WaterVertex) * vertices.size();
    m_VertexBuffer->CopyToMapped(vertices.data(), bufferSize);
}
}