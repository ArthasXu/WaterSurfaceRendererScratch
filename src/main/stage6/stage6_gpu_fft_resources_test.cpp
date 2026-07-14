#include "core/Application.h"

#include "scene/water/gpu/GPUFFT2D.h"
#include "scene/water/sources/WSTessendorfCPU.h"

#include <GLFW/glfw3.h>
#include <glm/glm.hpp>

#include <exception>
#include <iostream>
#include <memory>

class GPUFFTResourcesTestApp : public core::Application
{
protected:
    void Start() override
    {
        water::TessendorfSpectrumParams params{};
        params.resolution = 64;
        params.patchLength = 256.0f;
        params.windDirection = glm::normalize(glm::vec2(1.0f, 0.25f));
        params.windSpeed = 25.0f;
        params.spectrumAmplitude = 2.5f;
        params.shortWaveDamping = 0.001f;
        params.gravity = 9.81f;
        params.choppyLambda = 1.0f;
        params.oppositeWindDamping = 0.07f;
        params.randomSeed = 1337;

        m_CPUReference = std::make_unique<water::WSTessendorfCPU>(params);

        m_GPUFFT = std::make_unique<water::GPUFFT2D>(
            GetPhysicalDevice(),
            GetDevice(),
            GetCommandPool(),
            GetDevice().GetGraphicsQueue(),
            params.resolution,
            GetMaxFramesInFlight(),
            *m_CPUReference
        );

        std::cout << "GPU FFT static resource upload: OK\n";
        std::cout << "Complex field size = "
            << m_GPUFFT->GetComplexFieldSize()
            << "\n";
        std::cout << "Packed field size = "
            << m_GPUFFT->GetPackedFieldSize()
            << "\n";

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
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;

        vkBeginCommandBuffer(commandBuffer, &beginInfo);
        vkEndCommandBuffer(commandBuffer);
    }

    void ShutdownApp() override
    {
        m_GPUFFT.reset();
        m_CPUReference.reset();
    }

private:
    std::unique_ptr<water::WSTessendorfCPU> m_CPUReference;
    std::unique_ptr<water::GPUFFT2D> m_GPUFFT;
};

int main()
{
    try{
        GPUFFTResourcesTestApp app;
        app.Run();
    }
    catch(const std::exception& e){
        std::cerr << e.what() << "\n";
        return 1;
    }

    return 0;
}