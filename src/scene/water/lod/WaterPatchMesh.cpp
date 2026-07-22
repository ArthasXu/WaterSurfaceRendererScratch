#include "scene/water/lod/WaterPatchMesh.h"

#include <cstddef>

namespace water
{
VkVertexInputBindingDescription
WaterPatchVertex::GetBindingDescription()
{
    VkVertexInputBindingDescription binding{};
    binding.binding = 0;
    binding.stride = sizeof(WaterPatchVertex);
    binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
    return binding;
}

std::array<VkVertexInputAttributeDescription, 2>
WaterPatchVertex::GetAttributeDescriptions()
{
    std::array<VkVertexInputAttributeDescription, 2> attributes{};

    attributes[0].binding = 0;
    attributes[0].location = 0;
    attributes[0].format = VK_FORMAT_R32G32_SFLOAT;
    attributes[0].offset = offsetof(WaterPatchVertex, localXZ);

    attributes[1].binding = 0;
    attributes[1].location = 1;
    attributes[1].format = VK_FORMAT_R32_SFLOAT;
    attributes[1].offset = offsetof(WaterPatchVertex, skirt);

    return attributes;
}

WaterPatchMesh::WaterPatchMesh(
    VkPhysicalDevice physicalDevice,
    VkDevice device,
    vkp::CommandPool& commandPool,
    VkQueue graphicsQueue,
    uint32_t cellCount
)
{
    GeneratePatch(cellCount);
    CreateVertexBuffer(
        physicalDevice,
        device,
        commandPool,
        graphicsQueue
    );
    CreateIndexBuffer(
        physicalDevice,
        device,
        commandPool,
        graphicsQueue
    );
}

// 核心的几何体生成函数，负责构建整个补丁的顶点和索引数组
    // 1. 生成主体网格：生成一个 (cellCount+1) × (cellCount+1) 的规整四边形网格，
    // 每个顶点的 localXZ 从 (0,0) 均匀分布到 (1,1)，skirt 标记为 0.0。然后为每个四边形生成两个三角形索引。
    // 2. 生成裙边顶点：在主体网格的上、下、左、右四条边界外部，各创建一行“复制”的边界顶点。
    // 这些新顶点的 localXZ 与边界顶点相同，但 skirt 标记为 1.0。
    // 3. 缝合裙边三角形：在每个边界上，用原始边界顶点和新的裙边顶点构造三角形。
    // 这些三角形将主体网格的边界与裙边顶点连接起来。
    // 在顶点着色器中，裙边顶点会被向下推，从而在网格边缘形成一圈垂直的“帷幕”，
    // 有效遮挡不同 LOD 层级之间因细分程度不同而产生的裂缝。
void WaterPatchMesh::GeneratePatch(uint32_t cellCount)
{
    const uint32_t vertexCount = cellCount + 1;

    m_Vertices.clear();
    m_Indices.clear();

    m_Vertices.reserve(vertexCount * vertexCount + vertexCount * 4);

    for(uint32_t z = 0; z < vertexCount; ++z){
        for(uint32_t x = 0; x < vertexCount; ++x){
            WaterPatchVertex vertex{};
            vertex.localXZ = glm::vec2(
                static_cast<float>(x) / static_cast<float>(cellCount),
                static_cast<float>(z) / static_cast<float>(cellCount)
            );
            vertex.skirt = 0.0f;

            m_Vertices.push_back(vertex);
        }
    }

    for(uint32_t z = 0; z < cellCount; ++z){
        for(uint32_t x = 0; x < cellCount; ++x){
            uint32_t i0 = z * vertexCount + x;
            uint32_t i1 = z * vertexCount + x + 1;
            uint32_t i2 = (z + 1) * vertexCount + x;
            uint32_t i3 = (z + 1) * vertexCount + x + 1;

            m_Indices.push_back(i0);
            m_Indices.push_back(i2);
            m_Indices.push_back(i1);

            m_Indices.push_back(i1);
            m_Indices.push_back(i2);
            m_Indices.push_back(i3);
        }
    }

    auto addSkirtVertex =
        [this](glm::vec2 localXZ)
        {
            WaterPatchVertex vertex{};
            vertex.localXZ = localXZ;
            vertex.skirt = 1.0f;

            uint32_t index =
                static_cast<uint32_t>(m_Vertices.size());

            m_Vertices.push_back(vertex);

            return index;
        };

    for(uint32_t x = 0; x < cellCount; ++x){
        uint32_t top0 = x;
        uint32_t top1 = x + 1;

        uint32_t skirt0 =
            addSkirtVertex(m_Vertices[top0].localXZ);

        uint32_t skirt1 =
            addSkirtVertex(m_Vertices[top1].localXZ);

        m_Indices.push_back(top0);
        m_Indices.push_back(top1);
        m_Indices.push_back(skirt0);

        m_Indices.push_back(skirt0);
        m_Indices.push_back(top1);
        m_Indices.push_back(skirt1);
    }

    for(uint32_t x = 0; x < cellCount; ++x){
        uint32_t bottom0 = cellCount * vertexCount + x;
        uint32_t bottom1 = cellCount * vertexCount + x + 1;

        uint32_t skirt0 =
            addSkirtVertex(m_Vertices[bottom0].localXZ);

        uint32_t skirt1 =
            addSkirtVertex(m_Vertices[bottom1].localXZ);

        m_Indices.push_back(bottom0);
        m_Indices.push_back(skirt0);
        m_Indices.push_back(bottom1);

        m_Indices.push_back(skirt0);
        m_Indices.push_back(skirt1);
        m_Indices.push_back(bottom1);
    }

    for(uint32_t z = 0; z < cellCount; ++z){
        uint32_t left0 = z * vertexCount;
        uint32_t left1 = (z + 1) * vertexCount;

        uint32_t skirt0 =
            addSkirtVertex(m_Vertices[left0].localXZ);

        uint32_t skirt1 =
            addSkirtVertex(m_Vertices[left1].localXZ);

        m_Indices.push_back(left0);
        m_Indices.push_back(skirt0);
        m_Indices.push_back(left1);

        m_Indices.push_back(skirt0);
        m_Indices.push_back(skirt1);
        m_Indices.push_back(left1);
    }

    for(uint32_t z = 0; z < cellCount; ++z){
        uint32_t right0 = z * vertexCount + cellCount;
        uint32_t right1 = (z + 1) * vertexCount + cellCount;

        uint32_t skirt0 =
            addSkirtVertex(m_Vertices[right0].localXZ);

        uint32_t skirt1 =
            addSkirtVertex(m_Vertices[right1].localXZ);

        m_Indices.push_back(right0);
        m_Indices.push_back(right1);
        m_Indices.push_back(skirt0);

        m_Indices.push_back(skirt0);
        m_Indices.push_back(right1);
        m_Indices.push_back(skirt1);
    }

    m_IndexCount =
        static_cast<uint32_t>(m_Indices.size());
}

void WaterPatchMesh::CreateVertexBuffer(
    VkPhysicalDevice physicalDevice,
    VkDevice device,
    vkp::CommandPool& commandPool,
    VkQueue graphicsQueue
)
{
    VkDeviceSize bufferSize =
        sizeof(WaterPatchVertex) *
        m_Vertices.size();

    vkp::Buffer stagingBuffer(
        physicalDevice,
        device,
        bufferSize,
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
            VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
    );

    stagingBuffer.Map();
    stagingBuffer.CopyToMapped(m_Vertices.data(), bufferSize);
    stagingBuffer.Unmap();

    m_VertexBuffer = std::make_unique<vkp::Buffer>(
        physicalDevice,
        device,
        bufferSize,
        VK_BUFFER_USAGE_TRANSFER_DST_BIT |
            VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT
    );

    commandPool.CopyBuffer(
        device,
        graphicsQueue,
        stagingBuffer,
        *m_VertexBuffer,
        bufferSize
    );
}

void WaterPatchMesh::CreateIndexBuffer(
    VkPhysicalDevice physicalDevice,
    VkDevice device,
    vkp::CommandPool& commandPool,
    VkQueue graphicsQueue
)
{
    VkDeviceSize bufferSize =
        sizeof(uint32_t) *
        m_Indices.size();

    vkp::Buffer stagingBuffer(
        physicalDevice,
        device,
        bufferSize,
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
            VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
    );

    stagingBuffer.Map();
    stagingBuffer.CopyToMapped(m_Indices.data(), bufferSize);
    stagingBuffer.Unmap();

    m_IndexBuffer = std::make_unique<vkp::Buffer>(
        physicalDevice,
        device,
        bufferSize,
        VK_BUFFER_USAGE_TRANSFER_DST_BIT |
            VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT
    );

    commandPool.CopyBuffer(
        device,
        graphicsQueue,
        stagingBuffer,
        *m_IndexBuffer,
        bufferSize
    );
}

void WaterPatchMesh::Bind(VkCommandBuffer commandBuffer) const
{
    VkBuffer vertexBuffers[] = {*m_VertexBuffer};
    VkDeviceSize offsets[] = {0};

    vkCmdBindVertexBuffers(
        commandBuffer,
        0,
        1,
        vertexBuffers,
        offsets
    );

    vkCmdBindIndexBuffer(
        commandBuffer,
        *m_IndexBuffer,
        0,
        VK_INDEX_TYPE_UINT32
    );
}

void WaterPatchMesh::Draw(VkCommandBuffer commandBuffer) const
{
    vkCmdDrawIndexed(
        commandBuffer,
        m_IndexCount,
        1,
        0,
        0,
        0
    );
}

void WaterPatchMesh::DrawInstanced(
    VkCommandBuffer commandBuffer,
    uint32_t instanceCount
) const
{
    vkCmdDrawIndexed(
        commandBuffer,
        m_IndexCount,
        instanceCount,
        0,
        0,
        0
    );
}

uint32_t WaterPatchMesh::GetIndexCount() const
{
    return m_IndexCount;
}
}