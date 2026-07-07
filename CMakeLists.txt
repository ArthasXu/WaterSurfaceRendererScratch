cmake_minimum_required(VERSION 3.20)

project(WaterSurfaceRendererScratch LANGUAGES CXX)

find_package(Vulkan REQUIRED)
find_package(glfw3 CONFIG REQUIRED)
find_package(glm CONFIG REQUIRED)
find_package(spdlog CONFIG REQUIRED)
find_package(FFTW3 CONFIG REQUIRED)


# stage0_env
add_executable(stage0_env
    src/main/stage0/stage0_env.cpp
)

target_include_directories(stage0_env PRIVATE ${CMAKE_SOURCE_DIR}/src)

target_compile_features(stage0_env PRIVATE cxx_std_17)

target_link_libraries(stage0_env PRIVATE
    Vulkan::Vulkan
    glfw
    glm::glm
)


# stage1_triangle
add_executable(stage1_triangle
    src/main/stage1/stage1_triangle.cpp
)

target_include_directories(stage1_triangle PRIVATE ${CMAKE_SOURCE_DIR}/src)

target_compile_features(stage1_triangle PRIVATE cxx_std_17)

target_link_libraries(stage1_triangle PRIVATE
    Vulkan::Vulkan
    glfw
    glm::glm
)


# stage2_triangle
add_executable(stage2_triangle
    src/main/stage2/stage2_triangle.cpp
    src/vulkan/Instance.cpp
    src/vulkan/Surface.cpp
    src/vulkan/PhysicalDevice.cpp
    src/vulkan/Device.cpp
    src/vulkan/ImageView.cpp
    src/vulkan/Image.cpp
    src/vulkan/RenderPass.cpp
    src/vulkan/ShaderModule.cpp
    src/vulkan/Pipeline.cpp
    src/vulkan/SwapChain.cpp
    src/vulkan/CommandPool.cpp
    src/vulkan/CommandBuffer.cpp
    src/vulkan/SyncObjects.cpp
    src/vulkan/Buffer.cpp
)

target_include_directories(stage2_triangle PRIVATE ${CMAKE_SOURCE_DIR}/src)

target_compile_features(stage2_triangle PRIVATE cxx_std_17)

target_link_libraries(stage2_triangle PRIVATE
    Vulkan::Vulkan
    glfw
    glm::glm
)


# stage3_clear_color
add_executable(stage3_clear_color
    src/main/stage3/stage3_main.cpp
    src/main/stage3/ClearColorApp.cpp

    src/core/Application.cpp
    src/core/Window.cpp
    src/core/Timer.cpp
    src/core/Log.cpp

    src/scene/Camera.cpp

    src/vulkan/Instance.cpp
    src/vulkan/Surface.cpp
    src/vulkan/PhysicalDevice.cpp
    src/vulkan/Device.cpp
    src/vulkan/ImageView.cpp
    src/vulkan/Image.cpp
    src/vulkan/RenderPass.cpp
    src/vulkan/ShaderModule.cpp
    src/vulkan/Pipeline.cpp
    src/vulkan/SwapChain.cpp
    src/vulkan/CommandPool.cpp
    src/vulkan/CommandBuffer.cpp
    src/vulkan/SyncObjects.cpp
    src/vulkan/Buffer.cpp
)

target_include_directories(stage3_clear_color PRIVATE ${CMAKE_SOURCE_DIR}/src)

target_compile_features(stage3_clear_color PRIVATE cxx_std_17)

target_link_libraries(stage3_clear_color PRIVATE
    Vulkan::Vulkan
    glfw
    glm::glm
    spdlog::spdlog
)

# stage4_textured_quad
add_executable(stage4_textured_quad
    src/main/stage4/stage4_main.cpp

    src/core/Application.cpp
    src/core/Window.cpp
    src/core/Timer.cpp
    src/core/Log.cpp

    src/scene/Camera.cpp
    src/scene/Vertex.cpp
    src/scene/TexturedQuadApp.cpp

    src/vulkan/Instance.cpp
    src/vulkan/Surface.cpp
    src/vulkan/PhysicalDevice.cpp
    src/vulkan/Device.cpp
    src/vulkan/ImageView.cpp
    src/vulkan/RenderPass.cpp
    src/vulkan/ShaderModule.cpp
    src/vulkan/Pipeline.cpp
    src/vulkan/SwapChain.cpp
    src/vulkan/CommandPool.cpp
    src/vulkan/CommandBuffer.cpp
    src/vulkan/SyncObjects.cpp
    src/vulkan/Buffer.cpp
    src/vulkan/Descriptors.cpp
    src/vulkan/Image.cpp
    src/vulkan/Sampler.cpp
    src/vulkan/Texture2D.cpp
)

target_include_directories(stage4_textured_quad PRIVATE ${CMAKE_SOURCE_DIR}/src)

target_compile_features(stage4_textured_quad PRIVATE cxx_std_17)

target_link_libraries(stage4_textured_quad PRIVATE
    Vulkan::Vulkan
    glfw
    glm::glm
    spdlog::spdlog
)

# stage5_tessendorf_cpu
add_executable(stage5_tessendorf_cpu
    src/main/stage5/stage5_main.cpp
    src/scene/WSTessendorf.cpp
)

target_include_directories(stage5_tessendorf_cpu PRIVATE ${CMAKE_SOURCE_DIR}/src)

target_compile_features(stage5_tessendorf_cpu PRIVATE cxx_std_17)

target_link_libraries(stage5_tessendorf_cpu PRIVATE
    glm::glm
    spdlog::spdlog
    FFTW3::fftw3
)