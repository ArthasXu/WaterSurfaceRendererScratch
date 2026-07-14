#include "core/Application.h"

#include "scene/water/gpu/ComputePipeline.h"

#include <GLFW/glfw3.h>

#include <exception>
#include <iostream>
#include <memory>

class ComputePipelineTestApp : public core::Application
{
protected:
    void Start() override
    {
        water::ComputePipelineConfig config{};

        m_Pipeline = std::make_unique<water::ComputePipeline>(
            GetDevice(),
            "shaders/water/fft/empty.comp.spv",
            config
        );

        std::cout << "Compute pipeline created: OK\n";

        glfwSetWindowShouldClose(
            GetWindow().GetNativeWindow(),
            GLFW_TRUE
        );
    }

    void Update(core::Timestep timestep) override
    {
    }

    void Render(VkCommandBuffer commandBuffer, uint32_t imageIndex) override
    {
        VkCommandBufferBeginInfo beginInfo{};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO; // 结构体类型, 命令缓冲区开始信息

        vkBeginCommandBuffer(commandBuffer, &beginInfo);
        vkEndCommandBuffer(commandBuffer);
    }

    void ShutdownApp() override
    {
        m_Pipeline.reset();
    }

private:
    std::unique_ptr<water::ComputePipeline> m_Pipeline;
};

int main()
{
    try{
        ComputePipelineTestApp app;
        app.Run();
    }
    catch(const std::exception& e){
        std::cerr << e.what() << "\n";
        return 1;
    }

    return 0;
}