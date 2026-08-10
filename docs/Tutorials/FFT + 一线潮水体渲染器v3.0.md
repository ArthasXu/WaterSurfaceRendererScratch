# 从现有 Vulkan 渲染器到 FFT + Fluid Flux 式一线潮的完整实现文档

> 版本：v1.0  
> 适用项目：`WaterSurfaceRendererScratch` 自研 Vulkan 学习渲染器  
> 目标对齐：目标游戏项目现状为 **FFT 海面 + 波动方程交互 + 四叉树水面**；当前无法取得游戏项目源码，因此所有功能先在自研 Vulkan 渲染器中按可迁移的接口实现，避免形成与目标项目现状相冲突的第二套架构。  
> 本文依据《一线潮水体渲染器 v2.0》重新编排，重点吸收 Fluid Flux 的 **Front Field + Wave Profile + Foam Animation** 思路，而不是把一线潮简化为一条高斯高度函数。

---

## 1. 项目现状与本文边界

### 1.1 当前已经完成的能力

截至当前阶段，自研渲染器已经具有：

```text
Vulkan Instance / Surface / PhysicalDevice / Device
SwapChain / RenderPass / Pipeline
CommandPool / CommandBuffer / SyncObjects
Buffer / Image / ImageView / Sampler / Texture2D
DescriptorSetLayout / DescriptorPool / DescriptorWriter
窗口、输入、相机、主循环、resize
Vertex / Index Buffer
UBO
静态纹理
带贴图旋转四边形
Validation Layer 零错误
```

这意味着后续无需重做 Vulkan 基础，而应直接进入：

```text
3D 水面基础
→ FFT 数据源
→ 动态浮点纹理
→ Fluid Flux 式移动潮头
→ Wave Profile 几何位移
→ 泡沫动画
→ 水体 Shader
→ 大范围 LOD 适配
```

### 1.2 当前尚未完成的能力

```text
深度缓冲
3D 水面 Grid
动态浮点纹理更新
水面 displacement / slope 数据格式
CPU Tessendorf FFT
GPU/CPU FFT 适配层
Fluid Flux 式 Bore Front Field
2D Bore Wave Profile Texture
泡沫生成与动画
水体 Fresnel / 吸收 / 散射
1 km 级 LOD
交互波适配
暴风雨、雾、雨、闪电
```

### 1.3 本文不做的错误方向

第一版不应：

```text
先重写目标项目已经具有的波动方程
先重写完整四叉树
一开始直接实现 2D SWE / Boussinesq
用单个高斯峰冒充最终一线潮
用固定白线冒充泡沫
在局部 SWE 域内简单相加完整 FFT 高度并称为物理耦合
```

---

## 2. 总体策略：学习路线和产品路线并行

### 2.1 学习路线

```text
现有 Vulkan Stage 4
→ 固定 3D Grid
→ Gerstner 临时数据源
→ CPU Tessendorf FFT
→ 动态浮点纹理
→ 水面 Vertex Shader
→ 水体 Fragment Shader
```

目的：真正理解 FFT、频域导数、动态资源上传和水体渲染。

### 2.2 产品对齐路线

```text
统一数据接口
→ ExistingFFTAdapter 语义
→ BoreFrontField
→ Front Parameter LUT
→ BoreWaveProfile Texture
→ Foam Source
→ Foam Advection / Animation
→ 与现有四叉树接口对齐
→ 暴风雨水体 Shader
```

目的：在拿不到目标项目源码的情况下，仍然让每个模块能够被替换为已有 FFT、已有交互波和已有四叉树。

### 2.3 两条路线共享的核心数据

无论使用自研 CPU FFT 还是未来目标项目已有 FFT，Shader 端都只认统一语义：

```text
FFTDisplacementMap
FFTNormalAuxMap
BoreFrontParameters
FrontParameterLUT
BoreWaveProfileTexture
FoamSource
FlowVelocity
Camera / Water UBO
```

---

## 3. 最终系统架构

```text
                    ┌────────────────────────┐
                    │  Existing / CPU FFT    │
                    │ displacement, slope    │
                    └────────────┬───────────┘
                                 │
                    ┌────────────▼───────────┐
                    │   FFT Source Adapter    │
                    └────────────┬───────────┘
                                 │
┌───────────────────┐            │            ┌─────────────────────┐
│ BoreFrontField    │            │            │ Interaction Adapter │
│ signed distance   │            │            │ later               │
│ local normal      │            │            └──────────┬──────────┘
└─────────┬─────────┘            │                       │
          │                      │                       │
┌─────────▼─────────┐            │                       │
│ Front LUT         │            │                       │
│ offset/amplitude  │            │                       │
│ foam/phase        │            │                       │
└─────────┬─────────┘            │                       │
          │                      │                       │
┌─────────▼─────────┐            │                       │
│ Bore Wave Profile │            │                       │
│ forward/up/foam   │            │                       │
│ velocity          │            │                       │
└─────────┬─────────┘            │                       │
          └──────────────┬───────┴───────────────┬───────┘
                         ▼                       ▼
                 Water Surface Composer   Foam Source Composer
                         │                       │
                         ▼                       ▼
                 Vertex Displacement      Foam Appearance
                         │                       │
                         └──────────┬────────────┘
                                    ▼
                          Water Fragment Shader
                                    │
                                    ▼
                    Fresnel / absorption / sediment /
                    foam / fog / lightning / storm
```

---

## 4. 统一接口与数据契约

### 4.1 CPU 采样结构

```cpp
struct WaterSurfaceSample
{
    float height = 0.0f;
    glm::vec2 horizontalDisplacement{0.0f};
    glm::vec2 slope{0.0f};
    glm::vec2 velocity{0.0f};
    float foamSource = 0.0f;
    float blendMask = 1.0f;
};
```

### 4.2 水面数据源接口

```cpp
class IWaterSurfaceSource
{
public:
    virtual ~IWaterSurfaceSource() = default;

    virtual void Update(float deltaTime) = 0;
    virtual WaterSurfaceSample Sample(glm::vec2 worldXZ) const = 0;
};
```

### 4.3 FFT 适配接口

```cpp
class IFFTOceanSource : public IWaterSurfaceSource
{
public:
    virtual uint32_t GetResolution() const = 0;
    virtual float GetPatchLength() const = 0;

    virtual const std::vector<glm::vec4>& GetDisplacementMap() const = 0;
    virtual const std::vector<glm::vec4>& GetNormalAuxMap() const = 0;
};
```

这允许两种实现共存：

```text
WSTessendorfCPU       自研学习实现
ExistingFFTAdapter    未来对接游戏项目已有 FFT
```

### 4.4 GPU Binding 语义

第一版统一为：

```text
set 0, binding 0: CameraUBO
set 0, binding 1: WaterParamsUBO
set 0, binding 2: FFTDisplacementMap
set 0, binding 3: FFTNormalAuxMap
set 0, binding 4: FrontParameterLUT
set 0, binding 5: BoreWaveProfileTexture
set 0, binding 6: FoamDetailTexture
set 0, binding 7: LargeNormalTexture
set 0, binding 8: SmallNormalTexture
```

交互波后续增加：

```text
set 0, binding 9: InteractionHeightMap
set 0, binding 10: InteractionNormalMap
```

---

## 5. 推荐代码目录

```text
src/
  scene/
    water/
      common/
        WaterSurfaceTypes.h
        WaterSurfaceComposer.h
        WaterSurfaceComposer.cpp

      sources/
        IWaterSurfaceSource.h
        IFFTOceanSource.h
        WSGerstner.h
        WSGerstner.cpp
        WSTessendorf.h
        WSTessendorf.cpp
        ExistingFFTAdapter.h
        ExistingFFTAdapter.cpp
        BoreFrontField.h
        BoreFrontField.cpp
        BoreWaveProfile.h
        BoreWaveProfile.cpp

      render/
        WaterVertex.h
        WaterGrid.h
        WaterGrid.cpp
        WaterSurfaceMesh.h
        WaterSurfaceMesh.cpp
        DynamicImage2D.h
        DynamicImage2D.cpp
        DepthBuffer.h
        DepthBuffer.cpp

      foam/
        FoamAnimator.h
        FoamAnimator.cpp
        FoamAdvection.h
        FoamAdvection.cpp

      optional_physics/
        WSShallowWater1D.h
        WSShallowWater1D.cpp
        WSShallowWater2D.h
        WSShallowWater2D.cpp

  main/
    stage5_fft/
      stage5_fft_main.cpp
    stage6_water_grid/
      stage6_main.cpp
      WaterGridApp.h
      WaterGridApp.cpp
    stage7_bore/
      stage7_main.cpp
      BoreWaterApp.h
      BoreWaterApp.cpp
    stage8_water_shader/
      stage8_main.cpp
      StormWaterApp.h
      StormWaterApp.cpp

shaders/
  water/
    water.vert
    water.frag
    water_common.glsl
    fft_source.glsl
    bore_front.glsl
    bore_wave_profile.glsl
    foam_common.glsl
    foam_advection.comp
```

---

# 6. 总体阶段路线

| 阶段 | 核心输出 | 是否阻塞第一版 Demo |
|---|---|---:|
| P0 | Stage 4 冻结、接口文档 | 是 |
| P1 | 深度缓冲 + 固定 3D Grid | 是 |
| P2 | Gerstner 临时海面 | 建议 |
| P3 | CPU Tessendorf FFT | 是，学习版；产品版可由已有 FFT 代替 |
| P4 | DynamicImage2D + FFT GPU 接入 | 是 |
| P5 | BoreFrontField + Front LUT | 是 |
| P6 | BoreWaveProfile | 是 |
| P7 | Foam Source + Foam Animation | 是 |
| P8 | 水体 Shader | 是 |
| P9 | 1 km LOD / Quadtree 适配 | 是，最终性能版 |
| P10 | 交互波适配 | 后置 |
| P11 | 暴风雨、雾、雨、闪电 | 是，画面版 |
| P12 | 可选 1D SWE | 否 |
| P13 | 可选 2D SWE / Boussinesq | 否 |

---

# 7. P0：冻结当前 Stage 4 与建立接口文档

## P0.1 冻结当前版本

```bat
git add .
git commit -m "Stage 4: complete texture descriptor and UBO pipeline"
git tag stage4-complete
git switch -c water-bore-mainline
```

## P0.2 建立 `ExistingWaterSystemInterface.md`

即使拿不到游戏项目源码，也要预先记录未来需要确认的接口：

```text
FFT 输出是 CPU 数组还是 GPU Texture？
FFT displacement 格式？
FFT normal/slope 格式？
FFT patch 世界尺寸？
FFT UV 周期？
FFT 原点是否跟随相机？
波动方程输出高度还是速度？
四叉树 vertex shader 中世界坐标在哪里生成？
水面 tile 是否共享同一 descriptor？
水面材质是否使用 bindless？
```

## P0.3 定义“不可偏离目标项目现状”的规则

```text
自研 FFT 必须实现为 IFFTOceanSource
自研固定 Grid 只用于验证，不被视为最终 Quadtree 替代
交互波实现放到后面，并通过适配器接入
一线潮只能作为独立 Bore source
Shader 使用世界坐标，不依赖单块网格 UV
```

### P0 验收

```text
[ ] stage4-complete tag 存在
[ ] 统一数据源接口定义完成
[ ] Binding 语义写入文档
[ ] 自研模块与目标项目适配器边界明确
```

---

# 8. P1：深度缓冲与固定 3D 水面 Grid

## P1.1 增加深度格式查询

在 `Device` 或工具函数中增加：

```cpp
VkFormat FindSupportedFormat(...);
VkFormat FindDepthFormat();
```

优先：

```text
VK_FORMAT_D32_SFLOAT
VK_FORMAT_D32_SFLOAT_S8_UINT
VK_FORMAT_D24_UNORM_S8_UINT
```

## P1.2 实现 `DepthBuffer`

成员：

```text
Image
ImageView
VkFormat
width / height
```

usage：

```text
VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT
```

## P1.3 扩展 RenderPass

增加 depth attachment：

```text
loadOp = CLEAR
storeOp = DONT_CARE
initialLayout = UNDEFINED
finalLayout = DEPTH_STENCIL_ATTACHMENT_OPTIMAL
```

## P1.4 扩展 Framebuffer

每个 framebuffer 绑定：

```text
swapchain color ImageView
shared depth ImageView
```

## P1.5 扩展 Pipeline depth state

```text
depthTestEnable = VK_TRUE
depthWriteEnable = VK_TRUE
depthCompareOp = VK_COMPARE_OP_LESS
```

## P1.6 定义 WaterVertex

```cpp
struct WaterVertex
{
    glm::vec3 position;
    glm::vec2 uv;
};
```

## P1.7 生成固定 XZ Grid

第一版：

```text
129 × 129 顶点
128 × 128 quad
每个 quad 两个 triangle
世界尺寸 256 m × 256 m
```

## P1.8 上传 vertex/index buffer

沿用 Stage 4：

```text
host-visible staging
→ vkCmdCopyBuffer
→ device-local vertex/index buffer
```

## P1.9 创建 Debug Grid Shader

Vertex Shader 只做 MVP。

Fragment Shader 输出：

```text
世界坐标棋盘色
或 height=0 的纯色
```

## P1.10 增加线框调试

若设备支持：

```text
fillModeNonSolid
polygonMode = LINE
```

也可以使用 ImGui 前先用键盘切换 pipeline。

## P1.11 Resize 重建深度资源

顺序：

```text
wait device idle
销毁 framebuffer
销毁 depth image/view
重建 swapchain
重建 depth buffer
重建 framebuffer
```

### P1 验收

```text
[ ] 固定 Grid 在 XZ 平面正确显示
[ ] 相机可飞行观察
[ ] 深度遮挡正确
[ ] 线框拓扑正确
[ ] resize/minimize/restore 正常
[ ] validation 零错误
```

建议 commit：

```text
P1.1 add depth format and depth image
P1.2 extend render pass and framebuffer with depth
P1.3 add fixed water grid
P1.4 render water grid in wireframe
```

---

# 9. P2：Gerstner 临时数据源

这一阶段不是最终方案，而是为 FFT 接入前验证世界坐标位移、坡度和 shader 数据组合。

## P2.1 定义 Gerstner 参数

```cpp
struct GerstnerWave
{
    glm::vec2 direction;
    float amplitude;
    float wavelength;
    float speed;
    float steepness;
};
```

## P2.2 CPU 与 Shader 保持同一公式

第一版使用 4 条波：

```text
两个主风向波
两个斜向小波
```

## P2.3 输出统一 WaterSurfaceSample

```text
height
horizontal displacement
slope
velocity hint
foamSource=0
```

## P2.4 在 vertex shader 中位移 Grid

验收不是“好看”，而是：

```text
世界坐标稳定
相机移动不导致波纹游离
Grid tile UV 不影响波形相位
法线方向正确
```

### P2 验收

```text
[ ] Grid 出现可控动态波
[ ] 位移与法线一致
[ ] 所有波以世界坐标计算
[ ] 可随时替换为 FFT source
```

---

# 10. P3：CPU Tessendorf FFT 学习实现

> 产品目标中未来可以由 ExistingFFTAdapter 替换，但自研渲染器需要完成这一阶段，以理解当前游戏项目的 FFT 海面。

## P3.1 建立纯 CPU target

```text
stage5_fft_cpu
```

只链接：

```text
glm
spdlog
fftw3f
```

不要链接 Vulkan 和 GLFW。

## P3.2 定义参数

```cpp
struct TessendorfParams
{
    uint32_t resolution = 64;
    float patchLength = 256.0f;
    glm::vec2 windDirection{1.0f, 0.0f};
    float windSpeed = 25.0f;
    float amplitude = 0.0005f;
    float damping = 0.001f;
    float gravity = 9.81f;
    float choppyLambda = 1.0f;
    uint32_t randomSeed = 1337;
};
```

## P3.3 生成频率索引与波矢量

```cpp
int nx = x <= N / 2 ? x : int(x) - int(N);
int nz = z <= N / 2 ? z : int(z) - int(N);
```

```text
kx = 2π nx / L
kz = 2π nz / L
```

## P3.4 实现确定性高斯随机数

```text
std::mt19937
std::normal_distribution<float>
```

## P3.5 实现 Phillips 谱

```text
P(k) = A exp[-1/(kLw)^2] / k^4
       · dot(khat, what)^2
       · exp(-k^2 l^2)
```

特判 `k=0`。

## P3.6 生成 h0(k)

```text
h0(k) = 1/sqrt(2) · (ξr+iξi) · sqrt(P(k))
```

分两遍填充 `h0(k)` 与 `conj(h0(-k))`。

## P3.7 实现色散关系

```text
ω(k) = sqrt(g|k|)
```

## P3.8 实现时间演化

```text
h(k,t) = h0(k)e^(iωt) + conj(h0(-k))e^(-iωt)
```

## P3.9 先实现朴素 IDFT

使用 `N=16/32` 验证：

```text
高度实部
虚部残差
最小值/最大值
NaN/Inf
```

## P3.10 接 FFTW

要求：

```text
naive IDFT 与 FFTW 最大误差可接受
FFTW backward 后除以 N*N
```

## P3.11 计算频域导数

生成：

```text
height
slopeX / slopeZ
dispX / dispZ
dDxdx / dDzdz / dDxdz
Jacobian
```

## P3.12 输出统一纹理数组

```cpp
std::vector<glm::vec4> displacement;
// Dx, height, Dz, Jacobian

std::vector<glm::vec4> normalAux;
// slopeX, slopeZ, dDxdx, dDzdz
```

## P3.13 输出调试图

```text
height.pgm
dispX.pgm
dispZ.pgm
slopeX.pgm
slopeZ.pgm
jacobian.pgm
```

## P3.14 暴风雨参数测试

固定测试：

```text
windSpeed = 8 / 20 / 35 m/s
windDirection = (1,0) / normalize(1,1)
```

### P3 验收

```text
[ ] CPU target 独立运行
[ ] naive IDFT 与 FFTW 一致
[ ] 输出 displacement / normalAux
[ ] 风向改变后统计方向改变
[ ] 高风速下大尺度波增强
[ ] 无 NaN / Inf / 爆点
```

---

# 11. P4：DynamicImage2D 与 FFT 接入 Vulkan

## P4.1 不复用静态 Texture2D 的全部逻辑

新增：

```cpp
class DynamicImage2D
```

职责：

```text
创建浮点 VkImage
创建 ImageView / Sampler
维护每帧 staging buffer
支持每帧 CopyFromCPU
执行 transfer-write → shader-read barrier
```

## P4.2 第一版格式

```text
FFT displacement: VK_FORMAT_R32G32B32A32_SFLOAT
FFT normalAux:    VK_FORMAT_R32G32B32A32_SFLOAT
```

优化后可改 `RGBA16F`。

## P4.3 每飞行帧一份 staging

```text
Frame 0 staging
Frame 1 staging
Frame 2 staging
```

每份大小：

```text
N × N × sizeof(glm::vec4)
```

## P4.4 保持 staging mapped

避免每帧重复 map/unmap。

## P4.5 更新流程

```text
CPU ComputeWaves(t)
→ memcpy 到 current-frame staging
→ image layout shader-read → transfer-dst
→ vkCmdCopyBufferToImage
→ image layout transfer-dst → shader-read
→ vertex shader sample
```

## P4.6 descriptor 扩展

增加：

```text
binding 2 FFTDisplacementMap
binding 3 FFTNormalAuxMap
```

## P4.7 Vertex Shader 接入

```glsl
vec4 fftDisp = texture(fftDisplacementMap, fftUV);
worldPos.xz += fftDisp.xz * horizontalScale;
worldPos.y  += fftDisp.y  * heightScale;
```

## P4.8 slope 构造法线

```glsl
vec2 fftSlope = texture(fftNormalAuxMap, fftUV).xy;
vec3 normal = normalize(vec3(-fftSlope.x, 1.0, -fftSlope.y));
```

## P4.9 世界坐标到 FFT UV

```glsl
vec2 fftUV = fract(worldXZ / fftPatchLength + fftUVOffset);
```

不能使用单块 Grid 本地 UV 作为最终世界相位。

## P4.10 第一版同步策略

允许先使用 `vkQueueWaitIdle` 验证正确，随后升级到：

```text
每帧 fence
明确 barrier
不阻塞 graphics queue
```

### P4 验收

```text
[ ] FFT Grid 动态起伏
[ ] 相机移动后世界相位稳定
[ ] FFT tile 接缝不明显
[ ] 动态纹理每帧更新正确
[ ] validation 零错误
[ ] 64/128 分辨率运行稳定
```

---

# 12. P5：BoreFrontField——1 km 移动潮头空间场

## P5.1 定义基础坐标

```text
n = 基础推进方向
τ = 前沿切向 = (-n.y, n.x)
p = worldXZ
origin = 参考原点
```

```text
a = dot(p-origin, τ)       沿前沿位置
ξ = dot(p-origin, n)       穿过前沿的位置
```

## P5.2 定义移动潮头

```text
ξfront(a,t) = speed·t + offsetCurve(a)
```

有符号距离：

```text
s = ξ - ξfront(a,t)
```

约定：

```text
s > 0 潮头前方
s = 0 潮头中心
s < 0 潮头后方
```

## P5.3 直线版先通过

第一版令：

```text
offsetCurve(a)=0
```

仅验证 1 km 长度、推进速度和世界坐标稳定性。

## P5.4 创建 FrontParameterLUT

推荐 1D `RGBA16F` 或第一版 `RGBA32F`：

```text
R = front offset
G = crest amplitude multiplier
B = foam multiplier
A = animation phase offset
```

## P5.5 轻微弯曲

`offsetCurve(a)` 可来自：

```text
8～32 个控制点
Catmull-Rom / cubic interpolation
或低频正弦叠加
```

## P5.6 正确计算局部法向

```text
∇s = n - offsetCurve'(a) τ
frontNormal = normalize(∇s)
```

必须使用 LUT 导数或解析导数，不能弯曲后仍使用固定方向。

## P5.7 沿前沿非同步变化

使用 LUT 的：

```text
amplitude multiplier
foam multiplier
phase offset
```

打破“1 km 全线同高、同相位”的人工感。

## P5.8 BoreParamsUBO

```cpp
struct BoreParamsGPU
{
    glm::vec2 origin;
    glm::vec2 direction;

    float speed;
    float time;
    float frontLength;
    float profileWidth;

    float waterRise;
    float riseWidth;
    float crestFFTScale;
    float profileAnimationSpeed;

    glm::vec2 padding;
};
```

### P5 验收

```text
[ ] 1 km 潮头在世界坐标中推进
[ ] 不跟随 FFT tile 重复
[ ] 直线版稳定
[ ] 弯曲后局部法向正确
[ ] 沿前沿参数有低频变化
```

---

# 13. P6：Fluid Flux 式 Bore Wave Profile

## P6.1 Front Curve 与 Wave Profile 严格分离

```text
Front Curve：决定潮头在哪里
Wave Profile：决定横截面长什么样
```

## P6.2 定义 2D Wave Profile UV

```text
U = signed distance 映射到 [0,1]
V = animation phase
```

```glsl
float profileU = saturate(s / profileWidth * 0.5 + 0.5);
float profileV = fract(time * animationSpeed + phaseOffset);
```

## P6.3 通道语义

```text
R = 沿局部潮头法向的水平位移
G = 垂直位移
B = foam source
A = velocity / breaking / auxiliary
```

## P6.4 第一张测试 Profile

先程序生成 256×64 浮点纹理：

```text
U 方向：潮头前后横截面
V 方向：64 个动画相位
```

第一版可以包含：

```text
前方平水
快速抬升
高而窄的浪脊
浪脊后回落
后方两到三组衰减波列
泡沫集中在浪脊和回落区
```

## P6.5 生成方法分级

### 方法 A：程序化 LUT

快速验证。

### 方法 B：多条 2D 曲线烘焙

最终推荐：

```text
每一行/帧是一条横截面曲线
通过插值生成动画相位
烘焙 forward/upward/foam/velocity
```

### 方法 C：DCC/外部工具生成

后续可由 Blender、Python 曲线编辑器或 UE 原型生成，再加载到 Vulkan。

## P6.6 潮后整体水位单独处理

Wave Profile 负责浪头局部形状，平均水位抬升单独计算：

```glsl
float backMask = 1.0 - smoothstep(-riseWidth, riseWidth, s);
float waterRise = riseHeight * backMask;
```

## P6.7 顶点位移

```glsl
vec4 profile = texture(boreWaveProfile, vec2(profileU, profileV));

worldPos.xz += localFrontNormal * profile.r * horizontalScale;
worldPos.y  += profile.g * verticalScale + waterRise;
```

## P6.8 FFT crest suppression

浪脊中心降低 FFT 权重：

```glsl
float crestMask = ComputeCrestMask(profileU, profile.b);
float fftWeight = mix(1.0, crestFFTScale, crestMask);
```

推荐第一版：

```text
crestFFTScale = 0.3～0.5
```

最终位移：

```text
Pfinal = Pgrid + Dfft·fftWeight + Dbore
```

## P6.9 合并 slope 而不是单位法线

```glsl
vec2 totalSlope =
    fftSlope * fftWeight
  + boreSlope;

vec3 geometricNormal = normalize(vec3(-totalSlope.x, 1.0, -totalSlope.y));
```

### P6 验收

```text
[ ] 潮头不再只是高斯峰
[ ] 有可控翻卷/抬升/回落轮廓
[ ] 潮后平均水位抬高
[ ] FFT 不切断主潮头
[ ] 横截面动画相位连续
[ ] 弯曲前沿上的位移方向正确
```

---

# 14. P7：泡沫 Source 与泡沫动画

## P7.1 分离 Source 与 Appearance

```text
Foam Source：哪里产生泡沫
Foam Appearance：泡沫纹理如何流动、破碎和衰减
```

## P7.2 泡沫源

组合：

```text
profileFoam
slopeFoam
fftJacobianFoam
crestCompressionFoam
trailFoam
```

第一版：

```glsl
float foamSource = max(profile.b, slopeFoam);
```

## P7.3 视觉速度

Level 1 速度用于贴图动画，不声称物理守恒：

```glsl
vec2 boreVelocity =
    localFrontNormal *
    (baseFlow * backMask + crestFlow * foamSource);
```

## P7.4 三相位泡沫动画

相位：

```text
0
1/3
2/3
```

每个相位沿速度方向偏移采样：

```glsl
uv_i = worldXZ * foamScale - velocity * (time + phase_i) * foamSpeed;
```

通过周期权重平滑混合，避免单张纹理循环跳变。

## P7.5 多尺度泡沫

至少两层：

```text
大尺度团块
小尺度破碎细节
```

## P7.6 泡沫影响材质

```text
BaseColor      → 偏黄白
Roughness      → 提高
Specular       → 适度降低或扩大高光
Normal         → 加泡沫软高度法线
Opacity        → 视材质方案决定
LightningGain  → 闪电时增强
```

## P7.7 后续状态型泡沫

第二版可加入 ping-pong Foam Texture：

```text
Fnext(p) = Fprev(p-vdt) + source·dt - decay·Fprev·dt
```

第一版不阻塞 Demo。

### P7 验收

```text
[ ] 泡沫不是固定白线
[ ] 泡沫沿潮流方向移动
[ ] 浪脊泡沫最强
[ ] 后方泡沫衰减
[ ] 沿 1 km 前沿无明显同相位重复
```

---

# 15. P8：完整水体 Shader

实现顺序必须严格遵循：

```text
调试位移
→ 调试 slope/normal
→ 调试 foamSource
→ 基础水色
→ Fresnel
→ 天空反射
→ 吸收/散射
→ 泥沙浑浊
→ 泡沫材质
→ 雾
→ 闪电
```

## P8.1 基础 Fresnel

第一版使用 Schlick：

```text
F = F0 + (1-F0)(1-cosθ)^5
```

水 IOR 约 1.333。

## P8.2 天空反射

第一版可使用：

```text
程序化天空颜色
或 cubemap
```

## P8.3 水体颜色

照片目标偏浑浊棕灰：

```text
deepWaterColor
shallowWaterColor
sedimentColor
stormTint
```

## P8.4 吸收与散射

按水深或视线长度控制：

```text
浅处偏棕绿
深处偏蓝灰/深灰
```

## P8.5 多尺度法线

```text
FFT 几何 slope
LargeNormalMap
SmallNormalMap
FoamNormal
```

先合并微法线，再和几何法线组合。

## P8.6 泡沫

泡沫同时影响：

```text
颜色
粗糙度
高光
法线
透明度/遮罩
```

## P8.7 Debug 模式

必须保留：

```text
show FFT height
show bore signed distance
show profile U/V
show front local normal
show foam source
show final slope
show FFT weight suppression
```

### P8 验收

```text
[ ] 近景水体有层次
[ ] 一线潮轮廓在暗光下仍清楚
[ ] 泡沫与浪脊紧密对齐
[ ] 浑浊水色接近目标图像气质
[ ] Debug 视图能定位数据问题
```

---

# 16. P9：1 km 水面与目标四叉树对齐

## P9.1 固定 Grid 只用于验证

完成 P8 后，不继续无限加大固定 Grid。

## P9.2 定义 Quadtree Adapter 语义

即使目标源码不可得，也先定义：

```cpp
struct WaterTileDrawData
{
    glm::vec2 worldMin;
    glm::vec2 worldMax;
    uint32_t lodLevel;
    bool intersectsBoreFront;
};
```

## P9.3 所有一线潮计算必须使用世界坐标

这样从固定 Grid 切换到 Quadtree 时无需改变潮头逻辑。

## P9.4 Bore-aware LOD 指标

```text
camera distance
screen-space error
distance to bore front
profile slope magnitude
foam source intensity
```

## P9.5 潮头与 tile 相交测试

利用 tile AABB 在基础潮头坐标系中的范围判断：

```text
是否跨越 s≈0 的 profile width 区间
```

相交 tile 强制更高 LOD。

## P9.6 裂缝处理

第一版推荐：

```text
skirts
```

后续再做 stitching indices 或 geomorph。

## P9.7 远距离简化

```text
远处减少真实位移
保留 normal 与 foam ribbon
极远处可使用 impostor
```

### P9 验收

```text
[ ] 1 km 潮头连续
[ ] 潮头附近 LOD 足够
[ ] tile 切换不导致潮头相位跳变
[ ] 泡沫不随 tile UV 漂移
[ ] 目标帧率稳定
```

---

# 17. P10：交互波后置接入

由于目标游戏项目已有波动方程交互，这一阶段以适配器为主。

## P10.1 统一语义

```text
InteractionHeightMap
InteractionSlopeMap
InteractionMask
```

## P10.2 组合原则

第一版视觉组合：

```text
height = FFT + Bore + Interaction
```

但在潮头中心可降低 interaction 权重，避免局部波纹破坏主潮头形状。

## P10.3 世界坐标映射

交互波局部模拟域需要明确：

```text
worldMin
worldMax
resolution
UV transform
```

## P10.4 后续物理升级

若一线潮采用 SWE，则交互波与 SWE 的耦合另行设计，不在第一版中强行实现。

### P10 验收

```text
[ ] 局部点击/船体扰动可见
[ ] 不破坏一线潮主轮廓
[ ] 与 FFT 同时存在
[ ] 可通过 adapter 替换为目标项目已有资源
```

---

# 18. P11：暴风雨、雾、雨和闪电

## P11.1 暴风雨参数集合

```cpp
struct StormParams
{
    glm::vec3 skyColor;
    glm::vec3 fogColor;
    float fogDensity;
    float waterRoughness;
    float normalStrength;
    float sedimentAmount;
    float lightningIntensity;
};
```

## P11.2 风浪联动

```text
storm strength
→ FFT wind speed
→ normal strength
→ water roughness
→ foam threshold
```

## P11.3 雾

使用距离雾或指数雾：

```text
增强 1 km 尺度感
隐藏远处 LOD 简化
让潮头白线在远景中保持轮廓
```

## P11.4 雨

第一版：

```text
屏幕/世界空间雨线
雨滴法线扰动
```

交互水波后置。

## P11.5 闪电

短脉冲曲线：

```text
sky brightness
water specular
foam brightness
fog scattering
```

必须同时响应，避免只有天空闪烁。

### P11 验收

```text
[ ] 暴风雨氛围成立
[ ] 闪电同步照亮水面和泡沫
[ ] 远处潮头仍可辨认
[ ] 泡沫在暗环境下不发灰消失
```

---

# 19. P12：可选 1D SWE 物理升级

此阶段不阻塞第一版 Fluid Flux 式效果。

## P12.1 目标

用守恒横截面替换 Wave Profile 的平均水位、速度和冲击位置，但 Wave Profile 仍可作为视觉翻卷细节。

## P12.2 状态

```text
U = [h, hu]
```

## P12.3 通量

```text
F(U) = [hu, hu²/h + 0.5gh²]
```

## P12.4 Rusanov 通量

```text
F* = 0.5(FL+FR) - 0.5 amax(UR-UL)
```

## P12.5 CFL

```text
dt = CFL · dx / max(|u|+sqrt(gh))
```

## P12.6 测试

```text
静水
Dam Break
移动 Bore
质量守恒
非负水深
```

## P12.7 沿 1 km 前沿展开

第一版单截面；第二版 8～32 个截面沿前沿插值。

## P12.8 与 Wave Profile 分工

```text
SWE：平均水位、流速、潮头推进
Wave Profile：翻卷形状、局部位移、泡沫外观
```

### P12 验收

```text
[ ] 水深非负
[ ] 质量误差可监测
[ ] 潮头由 h/u 演化产生
[ ] Wave Profile 只作为视觉细节
```

---

# 20. P13：可选 2D SWE / Boussinesq

只有在以下需求明确出现时实施：

```text
河道地形影响
绕障碍传播
二维汇流
潮头断裂与重组
明显 undular bore
```

## P13.1 2D SWE

```text
U = [h, hu, hv]
```

先 CPU 小网格，完成：

```text
静水
二维 dam-break
CFL
positivity
地形源项
hydrostatic reconstruction
```

## P13.2 FFT 真正边界耦合

```text
FFT 长波低通
→ 边界水位时间序列
→ 近似法向速度
→ relaxation/sponge zone
→ SWE patch
```

局部域内使用 SWE 完整水面；只在重叠区视觉混合。

## P13.3 Boussinesq

只有明确需要色散性 undular bore 时再增加。

---

# 21. Shader 关键公式汇总

## 21.1 潮头坐标

```glsl
vec2 n = normalize(boreDirection);
vec2 t = vec2(-n.y, n.x);
vec2 rel = worldXZ - boreOrigin;

float alongFront = dot(rel, t);
float crossFront = dot(rel, n);
```

## 21.2 Front LUT

```glsl
vec4 frontData = texture(frontParameterLUT, vec2(frontU, 0.5));
float frontOffset = frontData.r;
float amplitudeScale = frontData.g;
float foamScale = frontData.b;
float phaseOffset = frontData.a;
```

## 21.3 Signed distance

```glsl
float signedDistance =
    crossFront - (boreSpeed * time + frontOffset);
```

## 21.4 Wave Profile

```glsl
float profileU = saturate(signedDistance / profileWidth * 0.5 + 0.5);
float profileV = fract(time * profileAnimSpeed + phaseOffset);
vec4 profile = texture(boreWaveProfile, vec2(profileU, profileV));
```

## 21.5 FFT suppression

```glsl
float crestMask = saturate(profile.b);
float fftWeight = mix(1.0, crestFFTScale, crestMask);
```

## 21.6 最终几何

```glsl
worldPos.xz += fftDisp.xz * fftWeight;
worldPos.y  += fftDisp.y  * fftWeight;

worldPos.xz += localFrontNormal * profile.r * amplitudeScale;
worldPos.y  += profile.g * amplitudeScale + waterRise;
```

## 21.7 坡度合并

```glsl
vec2 totalSlope =
    fftSlope * fftWeight
  + boreSlope
  + interactionSlope;

vec3 normal = normalize(vec3(-totalSlope.x, 1.0, -totalSlope.y));
```

## 21.8 泡沫

```glsl
float foamSource = max(profile.b * foamScale, slopeFoam);
float foamDetail = SampleThreePhaseFoam(worldXZ, velocity, time);
float foam = saturate(foamSource * foamDetail);
```

---

# 22. 资源格式建议

| 资源 | 学习版格式 | 优化版格式 |
|---|---|---|
| FFT displacement | RGBA32F | RGBA16F |
| FFT normalAux | RGBA32F | RGBA16F |
| Front parameter LUT | RGBA32F | RGBA16F |
| Bore Wave Profile | RGBA16F | RGBA16F |
| Foam detail | RGBA8 / BC | BC/压缩格式 |
| Large/Small normal | RGBA8 | BC5 |
| Interaction height | R32F | R16F |
| Interaction slope | RG32F | RG16F |
| SWE state | RGBA32F | 保持 RGBA32F |

---

# 23. 性能预算与升级路线

## 23.1 学习版

```text
FFT N=64/128
CPU FFT
RGBA32F 动态上传
固定 129×129 Grid
单 draw call
三相位泡沫采样
```

## 23.2 中间优化版

```text
FFT N=256
persistent mapped ring staging
每帧独立 image 或明确同步
RGBA16F
多环/Quadtree
Bore LUT + Profile 全 GPU 采样
```

## 23.3 生产对齐版

```text
已有 GPU FFT
已有波动方程
已有四叉树
BoreFrontField + WaveProfile 仅增加少量纹理采样
Foam compute/ping-pong 可选
无 CPU readback
```

## 23.4 预估主要成本

```text
Vertex：FFT 2 次采样 + Front LUT 1 次 + Profile 1 次
Fragment：2 层 normal + 2～3 相位泡沫 + 水体光学
CPU：Bore 参数更新极低
内存：Profile/LUT 极小
```

一线潮主体不应依赖 1 km 高分辨率二维贴图，因此其成本基本与水面顶点/像素数相关，而不是前沿世界长度直接相关。

---

# 24. 调试与验收矩阵

## 24.1 几何调试

```text
纯 Grid
只 FFT
只 Bore Front distance
只 Profile vertical displacement
只 Profile horizontal displacement
FFT + Bore
FFT suppression mask
```

## 24.2 法线调试

```text
只 FFT slope
只 Bore slope
合并 slope
微法线关闭/开启
```

## 24.3 泡沫调试

```text
profile foam source
slope foam
velocity direction
三相位权重
最终 foam
```

## 24.4 世界坐标稳定性

测试：

```text
相机移动
Grid tile 移动
resize
FFT UV offset
Bore origin 移动
前沿 1 km 长度
```

## 24.5 数值测试

FFT：

```text
NaN/Inf
Hermitian symmetry
IDFT/FFTW 误差
height range
```

## 24.6 Vulkan 测试

```text
validation layer 零错误
image layout 正确
staging 生命周期正确
descriptor 不引用已销毁资源
resize 后 depth/dynamic images 正确
```

---

# 25. 里程碑与 Definition of Done

## M1：3D 水面基础

```text
固定 Grid + Depth + Camera + Wireframe
```

## M2：FFT 海面

```text
CPU Tessendorf → dynamic image → vertex displacement
```

## M3：一线潮 MVP

```text
1 km BoreFrontField
+ Front LUT
+ Wave Profile
+ FFT suppression
```

这是第一版核心里程碑。

## M4：Fluid Flux 式泡沫

```text
Foam Source
+ Three-phase animated foam
+ 多尺度细节
```

## M5：完整水体材质

```text
Fresnel
+ reflection
+ absorption/scattering
+ sediment
+ foam material
```

## M6：1 km 性能版

```text
Quadtree/LOD adapter
+ bore-aware LOD
+ crack handling
```

## M7：暴风雨 Demo

```text
storm lighting
+ fog
+ rain
+ lightning
```

## 最终完成标准

```text
[ ] 暴风雨 FFT 海面自然起伏
[ ] 约 1 km 一线潮连续推进
[ ] 潮头有明确翻卷 Wave Profile
[ ] 潮后平均水位抬升
[ ] FFT 不破坏潮头主轮廓
[ ] 泡沫不是固定白线，而是有流动和破碎动画
[ ] 世界坐标稳定，切换 tile/LOD 不漂移
[ ] resize/minimize/restore 正常
[ ] validation layer 零错误
[ ] 接口可替换为已有 FFT、交互波和四叉树
```

---

# 26. 推荐排期

## 第 1 周：3D 水面基础

```text
Depth Buffer
Fixed Grid
Wireframe
World-coordinate debug
Gerstner temporary source
```

## 第 2 周：CPU FFT

```text
Phillips
h0
h(k,t)
naive IDFT
FFTW
displacement/normalAux
```

## 第 3 周：FFT Vulkan 接入

```text
DynamicImage2D
per-frame staging
float texture upload
vertex displacement
slope normal
```

## 第 4 周：一线潮 MVP

```text
BoreFrontField
Front LUT
straight → curved front
BoreParamsUBO
1 km world-space validation
```

## 第 5 周：Wave Profile

```text
2D profile generation
forward/upward/foam/velocity
water rise
FFT suppression
```

## 第 6 周：泡沫

```text
foam source
three-phase animation
multi-scale foam
material response
```

## 第 7 周：水体 Shader

```text
Fresnel
reflection
absorption/scattering
sediment
normal detail
```

## 第 8 周：LOD 与暴风雨

```text
Quadtree adapter semantics
bore-aware LOD
fog
rain
lightning
profiling
```

交互波、1D SWE、2D SWE 均放在此后。

---

# 27. 现在立即执行的第一批任务

严格只做下面 10 项：

```text
1. 给当前 Stage 4 打 tag。
2. 新建 water-bore-mainline 分支。
3. 新建 WaterSurfaceTypes 和 IWaterSurfaceSource。
4. 给现有 Application 增加 Depth Buffer 支持。
5. 建立 129×129 固定 XZ Grid。
6. 建立 WaterGridApp，并支持线框。
7. 在 shader 中输出世界坐标调试色。
8. 新建 WSGerstner，输出统一 WaterSurfaceSample。
9. 在 vertex shader 中接入 Gerstner 位移。
10. 完成 P1/P2 验收后再开始 CPU FFT。
```

第一批需要提交的文件：

```text
src/scene/water/common/WaterSurfaceTypes.h
src/scene/water/sources/IWaterSurfaceSource.h
src/scene/water/sources/WSGerstner.h/.cpp
src/scene/water/render/DepthBuffer.h/.cpp
src/scene/water/render/WaterGrid.h/.cpp
src/main/stage6_water_grid/WaterGridApp.h/.cpp
src/main/stage6_water_grid/stage6_main.cpp
shaders/water/water_grid_debug.vert
shaders/water/water_grid_debug.frag
CMakeLists.txt
```

第一批验收：

```text
平坦 3D Grid
+ 深度测试
+ 线框
+ 世界坐标稳定
+ Gerstner 临时波
+ resize 无问题
+ validation 零错误
```

---

# 28. 最终路线的一句话定义

> 在自研 Vulkan 渲染器中先完成可替换的 FFT 数据源、动态浮点纹理与固定水面网格；随后以世界坐标 `BoreFrontField` 定义约 1 km 移动潮头，用一维 Front LUT 控制潮线空间变化，用二维 Wave Profile Texture 控制浪头横截面动画，再以三相位流动泡沫和水体光学完成 Fluid Flux 式视觉表现；最终通过统一接口替换为目标项目已有 FFT、波动方程和四叉树，而不是另建一套与项目现状冲突的系统。
