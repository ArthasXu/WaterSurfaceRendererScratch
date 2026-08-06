vcpkg x-update-baseline --add-initial-baseline
vcpkg install

生成构建系统（VS2026）
cmake -S . -B build -G "Visual Studio 18 2026" -A x64 -DCMAKE_TOOLCHAIN_FILE=D:\vcpkg\scripts\buildsystems\vcpkg.cmake

编shader
# ===== 河流水面 =====
glslc -I shaders/water shaders/water/stage12_fluid_flux.frag -o shaders/water/stage12_fluid_flux.frag.spv
glslc -I shaders/water shaders/water/stage12_fluid_flux.vert -o shaders/water/stage12_fluid_flux.vert.spv

# ===== 泡沫 Compute =====
glslc -I shaders/water shaders/water/foam/foam_advect.comp -o shaders/water/foam/foam_advect.comp.spv
glslc -I shaders/water shaders/water/foam/foam_source.comp -o shaders/water/foam/foam_source.comp.spv

# ===== 河床地形 =====
glslc -I shaders/water shaders/water/terrain.frag -o shaders/water/terrain.frag.spv
glslc -I shaders/water shaders/water/terrain.vert -o shaders/water/terrain.vert.spv

# ===== 天空盒 =====
glslc -I shaders/water shaders/water/sky.vert -o shaders/water/sky.vert.spv
glslc -I shaders/water shaders/water/sky.frag -o shaders/water/sky.frag.spv

# ===== 白色潮脊 =====
glslc -I shaders/water shaders/water/bore_crest.vert -o shaders/water/bore_crest.vert.spv
glslc -I shaders/water shaders/water/bore_crest.frag -o shaders/water/bore_crest.frag.spv


增量编译
cmake --build build --config Debug --parallel         

vulkan画三角形：     .\build\Debug\stage2_triangle.exe         
渐变色背景：         .\build\Debug\stage3_clear_color.exe 
2D自转Texture：      .\build\Debug\stage4_textured_quad.exe 
G波水体网格：        .\build\Debug\stage5_water_grid.exe 

CPU FFT 水体网格：   .\build\Debug\stage6_fft_cpu_upload.exe 
GPU FFT 水体网格：   .\build\Debug\stage6_fft_gpu.exe 

Bore Front Field + LUT一线潮：  .\build\Debug\stage7_bore_front.exe 
Wave Profile一线潮： .\build\Debug\stage8_bore_profile.exe
加入浪潮泡沫和水体光学渲染：.\build\Debug\stage9_water.exe

实现Quadtree Water LOD(2048*2048m) + U形河流 Flow Map + 多潮头系统 + GUI：.\build\Debug\stage10_river_water.exe
实现Progress Map + 潮头水平面高度noise：.\build\Debug\stage11-progress-bore.exe
类Fluid Flux水体渲染优化：.\build\Debug\stage12-fluid-flux.exe

贴图生成器： .\build\Debug\river-field-baker.exe

git push origin stage12-visual-realism
git push -u gitlab stage12-visual-realism