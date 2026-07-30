生成构建系统
cmake -S . -B build -G "Visual Studio 18 2026" -A x64 -DCMAKE_TOOLCHAIN_FILE=D:\vcpkg\scripts\buildsystems\vcpkg.cmake

编shader
glslc -I shaders/water shaders/water/stage12_fluid_flux.frag -o shaders/water/stage12_fluid_flux.frag.spv
glslc -I shaders/water shaders/water/stage12_fluid_flux.vert -o shaders/water/stage12_fluid_flux.vert.spv
glslc shaders\water\foam\foam_advect.comp -o shaders\water\foam\foam_advect.comp.spv
glslc shaders\water\foam\foam_source.comp -o shaders\water\foam\foam_source.comp.spv

增量编译
cmake --build build --config Debug --parallel         

vulkan画三角形：     .\build\Debug\stage2_triangle.exe         
渐变色背景：         .\build\Debug\stage3_clear_color.exe 
2D自转Texture：      .\build\Debug\stage4_textured_quad.exe 
G波水体网格：        .\build\Debug\stage5_water_grid.exe 

FFT 水体参数：      src\scene\water\common\Stage6OceanConfig.h
CPU FFT 水体网格：   .\build\Debug\stage6_fft_cpu_upload.exe 
GPU FFT 水体网格：   .\build\Debug\stage6_fft_gpu.exe 

一线潮参数UBO： App::UpdateBoreFrontUniformBuffer/UpdateBoreProfileUniformBuffer/UpdateFoamParamsUniformBuffer/UpdateFoamSimulationUniformBuffer/UpdateWaterMaterialUniformBuffer
Bore Front Field + LUT一线潮：  .\build\Debug\stage7_bore_front.exe 
Wave Profile一线潮： .\build\Debug\stage8_bore_profile.exe
加入浪潮泡沫和水体光学渲染：.\build\Debug\stage9_water.exe

Quadtree Water LOD配置参数：src\scene\water\lod\WaterQuadtree.h - struct WaterQuadtreeConfig
Flow Map 初始化： src\main\stage10\Stage10RiverWaterApp.cpp - CreateRiverResources
实现Quadtree Water LOD(2048*2048m) + U形河流 Flow Map + 多潮头系统 + GUI：.\build\Debug\stage10_river_water.exe
实现Progress Map + 潮头水平面高度noise：.\build\Debug\stage11-progress-bore.exe
类Fluid Flux水体渲染优化：.\build\Debug\stage12-fluid-flux.exe

贴图生成器： .\build\Debug\river-field-baker.exe

git push origin stage8-bore-wave-profile
git push -u gitlab stage8-bore-wave-profile