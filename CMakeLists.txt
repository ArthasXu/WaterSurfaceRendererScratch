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

    src/scene/water/render/DepthBuffer.cpp

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
    src/scene/water/render/DepthBuffer.cpp

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
    src/scene/water/render/DepthBuffer.cpp

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

# stage5_water_grid
add_executable(stage5_water_grid
    src/main/stage5/stage5_main.cpp
    src/main/stage5/Stage5WaterGridApp.cpp

    src/core/Application.cpp
    src/core/Window.cpp
    src/core/Timer.cpp
    src/core/Log.cpp

    src/scene/Camera.cpp
    src/scene/water/common/WaterSurfaceComposer.cpp
    src/scene/water/render/DepthBuffer.cpp
    src/scene/water/render/WaterVertex.cpp
    src/scene/water/render/WaterGrid.cpp
    src/scene/water/sources/WSGerstnerCPU.cpp

    src/vulkan/Instance.cpp
    src/vulkan/Surface.cpp
    src/vulkan/PhysicalDevice.cpp
    src/vulkan/Device.cpp
    src/vulkan/Image.cpp
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
)

target_include_directories(stage5_water_grid PRIVATE ${CMAKE_SOURCE_DIR}/src)

target_compile_features(stage5_water_grid PRIVATE cxx_std_17)

target_link_libraries(stage5_water_grid PRIVATE
    Vulkan::Vulkan
    glfw
    glm::glm
    spdlog::spdlog
)

# stage5_fft_cpu
add_executable(stage5_fft_cpu
    src/main/stage5/fft_cpu_main.cpp

    src/scene/water/sources/WSTessendorfCPU.cpp
    src/scene/water/sources/WSTessendorfCascadesCPU.cpp

    src/scene/water/debug/ScalarFieldWriter.cpp
)

target_include_directories(stage5_fft_cpu
    PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)

target_compile_features(stage5_fft_cpu
    PRIVATE
    cxx_std_17
)

target_link_libraries(stage5_fft_cpu
    PRIVATE
    glm::glm
    spdlog::spdlog
    FFTW3::fftw3
)

# stage6_fft_cpu_upload
add_executable(stage6_fft_cpu_upload
    src/main/stage6/stage6_cpu_main.cpp
    src/main/stage6/Stage6CPUFFTApp.cpp

    src/core/Application.cpp
    src/core/Window.cpp
    src/core/Timer.cpp
    src/core/Log.cpp

    src/scene/Camera.cpp
    src/scene/water/render/DepthBuffer.cpp
    src/scene/water/render/WaterVertex.cpp
    src/scene/water/render/WaterGrid.cpp
    src/scene/water/render/DynamicImage2D.cpp
    src/scene/water/render/WaterSampler.cpp
    src/scene/water/sources/WSTessendorfCPU.cpp
    src/scene/water/sources/WSTessendorfCascadesCPU.cpp

    src/vulkan/Instance.cpp
    src/vulkan/Surface.cpp
    src/vulkan/PhysicalDevice.cpp
    src/vulkan/Device.cpp
    src/vulkan/Image.cpp
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
)

target_include_directories(stage6_fft_cpu_upload
    PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)

target_compile_features(stage6_fft_cpu_upload
    PRIVATE
    cxx_std_17
)

target_link_libraries(stage6_fft_cpu_upload
    PRIVATE
    Vulkan::Vulkan
    glfw
    glm::glm
    spdlog::spdlog
    FFTW3::fftw3
)

# stage6_stockham_cpu
add_executable(stage6_stockham_cpu
    src/main/stage6/stockham_cpu_main.cpp

    src/scene/water/gpu/StockhamFFTCPU.cpp
)

target_include_directories(stage6_stockham_cpu
    PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)

target_compile_features(stage6_stockham_cpu
    PRIVATE
    cxx_std_17
)

target_link_libraries(stage6_stockham_cpu
    PRIVATE
    glm::glm
    FFTW3::fftw3
)

# stage6_compute_pipeline_test
add_executable(stage6_compute_pipeline_test
    src/main/stage6/stage6_gpu_compute_pipeline_test.cpp

    src/core/Application.cpp
    src/core/Window.cpp
    src/core/Timer.cpp
    src/core/Log.cpp

    src/scene/water/render/DepthBuffer.cpp
    src/scene/water/gpu/ComputePipeline.cpp

    src/vulkan/Instance.cpp
    src/vulkan/Surface.cpp
    src/vulkan/PhysicalDevice.cpp
    src/vulkan/Device.cpp
    src/vulkan/Image.cpp
    src/vulkan/ImageView.cpp
    src/vulkan/RenderPass.cpp
    src/vulkan/ShaderModule.cpp
    src/vulkan/Pipeline.cpp
    src/vulkan/SwapChain.cpp
    src/vulkan/CommandPool.cpp
    src/vulkan/CommandBuffer.cpp
    src/vulkan/SyncObjects.cpp
    src/vulkan/Buffer.cpp
)

target_include_directories(stage6_compute_pipeline_test
    PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)

target_compile_features(stage6_compute_pipeline_test
    PRIVATE
    cxx_std_17
)

target_link_libraries(stage6_compute_pipeline_test
    PRIVATE
    Vulkan::Vulkan
    glfw
    glm::glm
    spdlog::spdlog
)

# stage6_gpu_fft_resources_test
add_executable(stage6_gpu_fft_resources_test
    src/main/stage6/stage6_gpu_fft_resources_test.cpp

    src/core/Application.cpp
    src/core/Window.cpp
    src/core/Timer.cpp
    src/core/Log.cpp

    src/scene/water/gpu/GPUFFT2D.cpp
    src/scene/water/render/DynamicImage2D.cpp
    src/scene/water/render/DepthBuffer.cpp
    src/scene/water/sources/WSTessendorfCPU.cpp

    src/vulkan/Instance.cpp
    src/vulkan/Surface.cpp
    src/vulkan/PhysicalDevice.cpp
    src/vulkan/Device.cpp
    src/vulkan/Image.cpp
    src/vulkan/ImageView.cpp
    src/vulkan/RenderPass.cpp
    src/vulkan/ShaderModule.cpp
    src/vulkan/Pipeline.cpp
    src/vulkan/SwapChain.cpp
    src/vulkan/CommandPool.cpp
    src/vulkan/CommandBuffer.cpp
    src/vulkan/SyncObjects.cpp
    src/vulkan/Buffer.cpp
)

target_include_directories(stage6_gpu_fft_resources_test
    PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)

target_compile_features(stage6_gpu_fft_resources_test
    PRIVATE
    cxx_std_17
)

target_link_libraries(stage6_gpu_fft_resources_test
    PRIVATE
    Vulkan::Vulkan
    glfw
    glm::glm
    spdlog::spdlog
    FFTW3::fftw3
)

# stage6_gpu_fft_height_test
add_executable(stage6_gpu_fft_height_test
    src/main/stage6/stage6_gpu_fft_height_test.cpp

    src/core/Application.cpp
    src/core/Window.cpp
    src/core/Timer.cpp
    src/core/Log.cpp

    src/scene/water/gpu/ComputePipeline.cpp
    src/scene/water/gpu/GPUFFT2D.cpp
    src/scene/water/render/DynamicImage2D.cpp
    src/scene/water/render/DepthBuffer.cpp
    src/scene/water/sources/WSTessendorfCPU.cpp

    src/vulkan/Instance.cpp
    src/vulkan/Surface.cpp
    src/vulkan/PhysicalDevice.cpp
    src/vulkan/Device.cpp
    src/vulkan/Image.cpp
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
)

target_include_directories(stage6_gpu_fft_height_test
    PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)

target_compile_features(stage6_gpu_fft_height_test
    PRIVATE
    cxx_std_17
)

target_link_libraries(stage6_gpu_fft_height_test
    PRIVATE
    Vulkan::Vulkan
    glfw
    glm::glm
    spdlog::spdlog
    FFTW3::fftw3
)

# stage6_gpu_fft_field_test
add_executable(stage6_gpu_fft_field_test
    src/main/stage6/stage6_gpu_fft_field_test.cpp

    src/core/Application.cpp
    src/core/Window.cpp
    src/core/Timer.cpp
    src/core/Log.cpp

    src/scene/water/gpu/ComputePipeline.cpp
    src/scene/water/gpu/GPUFFT2D.cpp
    src/scene/water/render/DynamicImage2D.cpp
    src/scene/water/render/DepthBuffer.cpp
    src/scene/water/sources/WSTessendorfCPU.cpp

    src/vulkan/Instance.cpp
    src/vulkan/Surface.cpp
    src/vulkan/PhysicalDevice.cpp
    src/vulkan/Device.cpp
    src/vulkan/Image.cpp
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
)

target_include_directories(stage6_gpu_fft_field_test
    PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)

target_compile_features(stage6_gpu_fft_field_test
    PRIVATE
    cxx_std_17
)

target_link_libraries(stage6_gpu_fft_field_test
    PRIVATE
    Vulkan::Vulkan
    glfw
    glm::glm
    spdlog::spdlog
    FFTW3::fftw3
)

# stage6_fft_gpu
add_executable(stage6_fft_gpu
    src/main/stage6/stage6_gpu_main.cpp
    src/main/stage6/Stage6GPUFFTApp.cpp

    src/core/Application.cpp
    src/core/Window.cpp
    src/core/Timer.cpp
    src/core/Log.cpp

    src/scene/Camera.cpp

    src/scene/water/gpu/ComputePipeline.cpp
    src/scene/water/gpu/GPUFFT2D.cpp
    src/scene/water/gpu/WSTessendorfGPU.cpp

    src/scene/water/render/DepthBuffer.cpp
    src/scene/water/render/DynamicImage2D.cpp
    src/scene/water/render/WaterGrid.cpp
    src/scene/water/render/WaterSampler.cpp
    src/scene/water/render/WaterVertex.cpp

    src/scene/water/sources/WSTessendorfCPU.cpp
    src/scene/water/sources/WSTessendorfCascadesCPU.cpp

    src/vulkan/Instance.cpp
    src/vulkan/Surface.cpp
    src/vulkan/PhysicalDevice.cpp
    src/vulkan/Device.cpp
    src/vulkan/Image.cpp
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
)

target_include_directories(stage6_fft_gpu
    PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)

target_compile_features(stage6_fft_gpu
    PRIVATE
    cxx_std_17
)

target_link_libraries(stage6_fft_gpu
    PRIVATE
    Vulkan::Vulkan
    glfw
    glm::glm
    spdlog::spdlog
    FFTW3::fftw3
)

# stage7_bore_front
add_executable(stage7_bore_front
    src/main/stage7/stage7_main.cpp
    src/main/stage7/Stage7BoreFrontApp.cpp

    src/core/Application.cpp
    src/core/Window.cpp
    src/core/Timer.cpp
    src/core/Log.cpp

    src/scene/Camera.cpp

    src/scene/water/gpu/ComputePipeline.cpp
    src/scene/water/gpu/GPUFFT2D.cpp
    src/scene/water/gpu/WSTessendorfGPU.cpp

    src/scene/water/render/DepthBuffer.cpp
    src/scene/water/render/DynamicImage2D.cpp
    src/scene/water/render/WaterGrid.cpp
    src/scene/water/render/WaterSampler.cpp
    src/scene/water/render/WaterVertex.cpp
    src/scene/water/render/StaticFloatTexture2D.cpp

    src/scene/water/bore/FrontParameterLUT.cpp
    src/scene/water/bore/BoreFrontField.cpp

    src/scene/water/sources/WSTessendorfCPU.cpp
    src/scene/water/sources/WSTessendorfCascadesCPU.cpp

    src/vulkan/Instance.cpp
    src/vulkan/Surface.cpp
    src/vulkan/PhysicalDevice.cpp
    src/vulkan/Image.cpp
    src/vulkan/ImageView.cpp
    src/vulkan/Device.cpp
    src/vulkan/RenderPass.cpp
    src/vulkan/ShaderModule.cpp
    src/vulkan/Pipeline.cpp
    src/vulkan/SwapChain.cpp
    src/vulkan/CommandPool.cpp
    src/vulkan/CommandBuffer.cpp
    src/vulkan/SyncObjects.cpp
    src/vulkan/Buffer.cpp
    src/vulkan/Descriptors.cpp
)

target_include_directories(stage7_bore_front
    PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)

target_compile_features(stage7_bore_front
    PRIVATE
    cxx_std_17
)

target_link_libraries(stage7_bore_front
    PRIVATE
    Vulkan::Vulkan
    glfw
    glm::glm
    spdlog::spdlog
    FFTW3::fftw3
)

# stage7_bore_front_cpu_test
add_executable(stage7_bore_front_cpu_test
    src/main/stage7/stage7_bore_front_cpu_test.cpp

    src/scene/water/bore/BoreFrontField.cpp
    src/scene/water/bore/FrontParameterLUT.cpp
)

target_include_directories(stage7_bore_front_cpu_test
    PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)

target_compile_features(stage7_bore_front_cpu_test
    PRIVATE
    cxx_std_17
)

target_link_libraries(stage7_bore_front_cpu_test
    PRIVATE
    glm::glm
)

# stage7_bore_front_debug
add_executable(stage7_bore_front_debug
    src/main/stage7/stage7_bore_front_debug_main.cpp

    src/scene/water/bore/BoreFrontField.cpp
    src/scene/water/bore/FrontParameterLUT.cpp
)

target_include_directories(stage7_bore_front_debug
    PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)

target_compile_features(stage7_bore_front_debug
    PRIVATE
    cxx_std_17
)

target_link_libraries(stage7_bore_front_debug
    PRIVATE
    glm::glm
)

# stage8_bore_profile
add_executable(stage8_bore_profile
    src/main/stage8/stage8_main.cpp
    src/main/stage8/Stage8BoreProfileApp.cpp

    src/core/Application.cpp
    src/core/Window.cpp
    src/core/Timer.cpp
    src/core/Log.cpp

    src/scene/Camera.cpp

    src/scene/water/gpu/ComputePipeline.cpp
    src/scene/water/gpu/GPUFFT2D.cpp
    src/scene/water/gpu/WSTessendorfGPU.cpp

    src/scene/water/render/DepthBuffer.cpp
    src/scene/water/render/DynamicImage2D.cpp
    src/scene/water/render/WaterGrid.cpp
    src/scene/water/render/WaterSampler.cpp
    src/scene/water/render/WaterVertex.cpp
    src/scene/water/render/StaticFloatTexture2D.cpp
    src/scene/water/render/StaticDataTexture2D.cpp

    src/scene/water/bore/FrontParameterLUT.cpp
    src/scene/water/bore/BoreFrontField.cpp
    src/scene/water/bore/BoreWaveProfile.cpp

    src/scene/water/sources/WSTessendorfCPU.cpp
    src/scene/water/sources/WSTessendorfCascadesCPU.cpp

    src/vulkan/Instance.cpp
    src/vulkan/Surface.cpp
    src/vulkan/PhysicalDevice.cpp
    src/vulkan/Image.cpp
    src/vulkan/ImageView.cpp
    src/vulkan/Device.cpp
    src/vulkan/RenderPass.cpp
    src/vulkan/ShaderModule.cpp
    src/vulkan/Pipeline.cpp
    src/vulkan/SwapChain.cpp
    src/vulkan/CommandPool.cpp
    src/vulkan/CommandBuffer.cpp
    src/vulkan/SyncObjects.cpp
    src/vulkan/Buffer.cpp
    src/vulkan/Descriptors.cpp
)

target_include_directories(stage8_bore_profile
    PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)

target_compile_features(stage8_bore_profile
    PRIVATE
    cxx_std_17
)

target_link_libraries(stage8_bore_profile
    PRIVATE
    Vulkan::Vulkan
    glfw
    glm::glm
    spdlog::spdlog
    FFTW3::fftw3
)

# stage8_bore_profile_cpu_test
add_executable(stage8_bore_profile_cpu_test
    src/main/stage8/stage8_profile_cpu_test.cpp

    src/scene/water/bore/BoreWaveProfile.cpp
    src/scene/water/debug/BoreProfileWriter.cpp
)

target_include_directories(stage8_bore_profile_cpu_test
    PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)

target_compile_features(stage8_bore_profile_cpu_test
    PRIVATE
    cxx_std_17
)

target_link_libraries(stage8_bore_profile_cpu_test
    PRIVATE
    glm::glm
)


# stage9_water
add_executable(stage9_water
    src/main/stage9/stage9_main.cpp
    src/main/stage9/Stage9WaterApp.cpp

    src/core/Application.cpp
    src/core/Window.cpp
    src/core/Timer.cpp
    src/core/Log.cpp

    src/scene/Camera.cpp

    src/scene/water/gpu/ComputePipeline.cpp
    src/scene/water/gpu/GPUFFT2D.cpp
    src/scene/water/gpu/WSTessendorfGPU.cpp

    src/scene/water/render/DepthBuffer.cpp
    src/scene/water/render/DynamicImage2D.cpp
    src/scene/water/render/WaterGrid.cpp
    src/scene/water/render/WaterSampler.cpp
    src/scene/water/render/WaterVertex.cpp
    src/scene/water/render/StaticFloatTexture2D.cpp
    src/scene/water/render/StaticDataTexture2D.cpp
    src/scene/water/render/ComputeImage2D.cpp

    src/scene/water/bore/FrontParameterLUT.cpp
    src/scene/water/bore/BoreFrontField.cpp
    src/scene/water/bore/BoreWaveProfile.cpp

    src/scene/water/foam/FoamDetailGenerator.cpp

    src/scene/water/sources/WSTessendorfCPU.cpp
    src/scene/water/sources/WSTessendorfCascadesCPU.cpp

    src/vulkan/Instance.cpp
    src/vulkan/Surface.cpp
    src/vulkan/PhysicalDevice.cpp
    src/vulkan/Image.cpp
    src/vulkan/ImageView.cpp
    src/vulkan/Device.cpp
    src/vulkan/RenderPass.cpp
    src/vulkan/ShaderModule.cpp
    src/vulkan/Pipeline.cpp
    src/vulkan/SwapChain.cpp
    src/vulkan/CommandPool.cpp
    src/vulkan/CommandBuffer.cpp
    src/vulkan/SyncObjects.cpp
    src/vulkan/Buffer.cpp
    src/vulkan/Descriptors.cpp
)

target_include_directories(stage9_water
    PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)

target_compile_features(stage9_water
    PRIVATE
    cxx_std_17
)

target_link_libraries(stage9_water
    PRIVATE
    Vulkan::Vulkan
    glfw
    glm::glm
    spdlog::spdlog
    FFTW3::fftw3
)


# stage9_foam_detail_cpu_test
add_executable(stage9_foam_detail_cpu_test
    src/main/stage9/stage9_foam_detail_cpu_test.cpp
    src/scene/water/foam/FoamDetailGenerator.cpp
)

target_include_directories(stage9_foam_detail_cpu_test
    PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)

target_compile_features(stage9_foam_detail_cpu_test
    PRIVATE
    cxx_std_17
)

target_link_libraries(stage9_foam_detail_cpu_test
    PRIVATE
    glm::glm
)


# stage10_river_water
add_executable(stage10_river_water
    src/main/stage10/stage10_main.cpp
    src/main/stage10/Stage10RiverWaterApp.cpp

    src/core/Application.cpp
    src/core/Window.cpp
    src/core/Timer.cpp
    src/core/Log.cpp

    src/scene/Camera.cpp

    src/scene/water/gpu/ComputePipeline.cpp
    src/scene/water/gpu/GPUFFT2D.cpp
    src/scene/water/gpu/WSTessendorfGPU.cpp

    src/scene/water/render/DepthBuffer.cpp
    src/scene/water/render/DynamicImage2D.cpp
    src/scene/water/render/WaterGrid.cpp
    src/scene/water/render/WaterSampler.cpp
    src/scene/water/render/WaterVertex.cpp
    src/scene/water/render/StaticFloatTexture2D.cpp
    src/scene/water/render/StaticDataTexture2D.cpp
    src/scene/water/render/ComputeImage2D.cpp

    src/scene/water/lod/WaterPatchMesh.cpp
    src/scene/water/lod/WaterQuadtree.cpp

    src/scene/water/river/RiverSpline.cpp
    src/scene/water/river/RiverFieldBaker.cpp
    src/scene/water/river/RiverField.cpp

    src/scene/water/bore/FrontParameterLUT.cpp
    src/scene/water/bore/BoreFrontField.cpp
    src/scene/water/bore/BoreWaveProfile.cpp

    src/scene/water/foam/FoamDetailGenerator.cpp

    src/scene/water/sources/WSTessendorfCPU.cpp
    src/scene/water/sources/WSTessendorfCascadesCPU.cpp

    src/vulkan/Instance.cpp
    src/vulkan/Surface.cpp
    src/vulkan/PhysicalDevice.cpp
    src/vulkan/Image.cpp
    src/vulkan/ImageView.cpp
    src/vulkan/Device.cpp
    src/vulkan/RenderPass.cpp
    src/vulkan/ShaderModule.cpp
    src/vulkan/Pipeline.cpp
    src/vulkan/SwapChain.cpp
    src/vulkan/CommandPool.cpp
    src/vulkan/CommandBuffer.cpp
    src/vulkan/SyncObjects.cpp
    src/vulkan/Buffer.cpp
    src/vulkan/Descriptors.cpp
)

target_include_directories(stage10_river_water
    PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)

target_compile_features(stage10_river_water
    PRIVATE
    cxx_std_17
)

target_link_libraries(stage10_river_water
    PRIVATE
    Vulkan::Vulkan
    glfw
    glm::glm
    spdlog::spdlog
    FFTW3::fftw3
)