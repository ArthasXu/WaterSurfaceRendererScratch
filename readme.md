cmake -S . -B build -G "Visual Studio 18 2026" -A x64 -DCMAKE_TOOLCHAIN_FILE=D:\vcpkg\scripts\buildsystems\vcpkg.cmake

glslc shaders\stage1_triangle.vert -o shaders\stage1_triangle.vert.spv
glslc shaders\stage1_triangle.frag -o shaders\stage1_triangle.frag.spv

glslc shaders\stage4_textured_quad.vert -o shaders\stage4_textured_quad.vert.spv
glslc shaders\stage4_textured_quad.frag -o shaders\stage4_textured_quad.frag.spv

cmake --build build --config Debug --parallel         

build\Debug\stage2_triangle.exe         