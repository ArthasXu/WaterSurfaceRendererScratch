cmake_minimum_required(VERSION 3.20)

project(WaterSurfaceRendererScratch LANGUAGES CXX)

find_package(Vulkan REQUIRED)
find_package(glfw3 CONFIG REQUIRED)
find_package(glm CONFIG REQUIRED)


# stage0_env
add_executable(stage0_env
    src/stage0_env.cpp
)

target_compile_features(stage0_env PRIVATE cxx_std_17)

target_link_libraries(stage0_env PRIVATE
    Vulkan::Vulkan
    glfw
    glm::glm
)


# stage1_triangle
add_executable(stage1_triangle
    src/stage1_triangle.cpp
)

target_compile_features(stage1_triangle PRIVATE cxx_std_17)

target_link_libraries(stage1_triangle PRIVATE
    Vulkan::Vulkan
    glfw
    glm::glm
)


# stage2_triangle
add_executable(stage2_triangle
    src/stage2_triangle.cpp
    src/vulkan/Instance.cpp
    src/vulkan/Surface.cpp
    src/vulkan/PhysicalDevice.cpp
)

target_compile_features(stage2_triangle PRIVATE cxx_std_17)

target_link_libraries(stage2_triangle PRIVATE
    Vulkan::Vulkan
    glfw
    glm::glm
)