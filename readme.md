生成构建系统
cmake -S . -B build -G "Visual Studio 18 2026" -A x64 -DCMAKE_TOOLCHAIN_FILE=D:\vcpkg\scripts\buildsystems\vcpkg.cmake

编shader
glslc shaders\stage4_textured_quad.vert -o shaders\stage4_textured_quad.vert.spv
glslc shaders\stage4_textured_quad.frag -o shaders\stage4_textured_quad.frag.spv

增量编译
cmake --build build --config Debug --parallel         

vulkan画三角形：    build\Debug\stage2_triangle.exe         
渐变色背景：        build\Debug\stage3_clear_color.exe 
2D自转Texture：     build\Debug\stage4_textured_quad.exe 
G波水体网格：       build\Debug\stage5_water_grid.exe 

FFT 水体参数：      src\scene\water\common\Stage6OceanConfig.h
CPU FFT 水体网格：  build\Debug\stage6_fft_cpu_upload.exe 
GPU FFT 水体网格：  build\Debug\stage6_fft_gpu.exe 

build\Debug\stage7_bore_front.exe 