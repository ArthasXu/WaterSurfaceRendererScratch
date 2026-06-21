cmake_minimum_required(VERSION 3.20)

project(WaterSurfaceRendererScratch LANGUAGES CXX)

add_executable(stage0_env
    src/main.cpp
)

target_compile_features(stage0_env PRIVATE cxx_std_17)

find_package(Vulkan REQUIRED)
find_package(glfw3 CONFIG REQUIRED)
find_package(glm CONFIG REQUIRED)

target_link_libraries(stage0_env PRIVATE
    Vulkan::Vulkan
    glfw
    glm::glm
)