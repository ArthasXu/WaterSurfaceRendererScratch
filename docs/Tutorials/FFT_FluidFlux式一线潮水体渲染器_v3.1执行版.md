# FFT + Fluid Flux 式一线潮水体渲染器 v3.1 执行版

> **版本：v3.1**  
> **适用仓库：** `WaterSurfaceRendererScratch`  
> **当前起点：** 已完成 Vulkan Stage 4——窗口与相机、Application 主循环、Swapchain、RenderPass、Pipeline、顶点/索引缓冲、UBO、Descriptor、静态纹理、staging 上传、resize 与 Validation Layer。  
> **目标对齐：** 目标游戏项目已存在 **FFT 海面 + 波动方程交互水波 + 四叉树水面**；由于无法取得项目源码，本执行版要求所有自研模块均通过可替换接口实现，禁止形成与目标项目现状相冲突的第二套生产架构。  
> **最终目标：** 在自研 Vulkan 渲染器中完整实现可验证的单层/多 Cascade FFT、GPU Compute FFT、Fluid Flux 式移动一线潮、Wave Profile、泡沫动画、水体 Shader 与 1 km LOD 验证；交互波放在主视觉链路完成之后接入。

---

## 1. 执行原则

### 1.1 学习实现与生产适配并行，但不混为一套接口

```text
学习线：
Gerstner
→ 单层 CPU Tessendorf FFT
→ 多 Cascade CPU FFT
→ CPU 动态纹理接入
→ GPU Compute FFT
→ 水体 Shader

产品对齐线：
Existing FFT GPU Resource Adapter
→ BoreFrontField
→ Front Parameter LUT
→ Bore Wave Profile
→ Foam Animation
→ Existing Quadtree Adapter
→ Existing Interaction Adapter（后置）
```

学习线用于理解和单元测试；产品对齐线用于保证未来能迁移到已有 GPU FFT、波动方程和四叉树中。CPU FFT 长期保留，作为 GPU FFT 的参考实现，不在 GPU 版本完成后删除。

### 1.2 第一版一线潮不是完整 SWE，但必须保留物理升级接口

第一版采用：

```text
FFT 背景风浪
+ 世界坐标移动前沿
+ 2D Wave Profile 几何动画
+ 潮后平均水位抬升
+ 泡沫生成与动画
```

其目标是复现 Fluid Flux 式可控视觉效果，不宣称质量和动量守恒。

后续可选升级：

```text
1D SWE 横截面替换 Wave Profile 的高度/速度
→ 必要时局部 2D SWE
→ 明确需要色散型 undular bore 时再做 Boussinesq
```

### 1.3 固定 Grid 只用于验证，不替代目标项目四叉树

固定 Grid 用来验证：

- 深度测试；
- 世界坐标；
- FFT 位移；
- Wave Profile；
- 法线与泡沫；
- Descriptor 与同步。

完整 1 km 一线潮最终验收必须在 Quadtree/LOD 或等价大范围网格上完成。

---

## 2. 最终系统架构

```text
                    CPU Reference Path
                    ┌──────────────────────┐
                    │ WSTessendorfCPU      │
                    │ single / cascades    │
                    └──────────┬───────────┘
                               │ validation / learning upload
                               ▼
┌────────────────────────────────────────────────────────────┐
│                 Unified Shader Resource Contract           │
│ FFT displacement / slope / Jacobian / patch metadata      │
└────────────────────────────────────────────────────────────┘
                               ▲
                               │ direct GPU resources
                    ┌──────────┴───────────┐
                    │ WSTessendorfGPU      │
                    │ ExistingFFTAdapter   │
                    └──────────┬───────────┘
                               │
                               ▼
┌──────────────────┐   ┌────────────────────┐   ┌──────────────────┐
│ BoreFrontField   │   │ BoreWaveProfile    │   │ Interaction      │
│ signed distance  │   │ displacement tex   │   │ Adapter later    │
│ local normal     │   │ derivative tex     │   │ height / slope   │
│ finite mask      │   │ foam / crest       │   └─────────┬────────┘
└─────────┬────────┘   └──────────┬─────────┘             │
          └──────────────┬────────┴────────────────────────┘
                         ▼
                Water Surface Composer
                         │
          ┌──────────────┴──────────────┐
          ▼                             ▼
   final displacement             foam source / flow
          │                             │
          ▼                             ▼
    Water Vertex Shader           Foam Appearance
          └──────────────┬──────────────┘
                         ▼
                  Water Fragment Shader
                         │
       Fresnel / reflection / absorption / sediment /
       foam roughness / fog / rain / lightning
```

---

## 3. CPU 与 GPU 数据源接口

### 3.1 通用 CPU 采样接口

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

class ICPUWaterSurfaceSource
{
public:
    virtual ~ICPUWaterSurfaceSource() = default;

    virtual void Update(float dt) = 0;
    virtual WaterSurfaceSample Sample(glm::vec2 worldXZ) const = 0;
};
```

适用实现：

```text
WSGerstnerCPU
WSTessendorfCPU
WSWaveEquationCPU（后置）
WSShallowWater1D（可选）
```

`WSTessendorfCPU` 可额外暴露 `CPUWaterSurfaceFrame`，用于 P4A 的纹理上传和 CPU/GPU 数值比较，但不能要求所有水面源都提供 CPU 数组。

### 3.2 GPU 资源接口

```cpp
struct WaterCascadeGPUResource
{
    VkDescriptorImageInfo displacement{};
    VkDescriptorImageInfo normalAux{};

    float patchLength = 0.0f;
    uint32_t resolution = 0;
    float amplitudeScale = 1.0f;
};

constexpr uint32_t kMaxFFTCascades = 3;

struct WaterSurfaceGPUResources
{
    std::array<WaterCascadeGPUResource, kMaxFFTCascades> cascades{};
    uint32_t cascadeCount = 0;
};

class IGPUWaterSurfaceSource
{
public:
    virtual ~IGPUWaterSurfaceSource() = default;

    virtual void UpdateGPU(
        VkCommandBuffer commandBuffer,
        float dt) = 0;

    virtual const WaterSurfaceGPUResources&
    GetGPUResources() const = 0;
};
```

适用实现：

```text
WSTessendorfGPU
ExistingFFTAdapter
```

必须遵守：

```text
GPU FFT 结果不能为了满足接口而读回 CPU。
ExistingFFTAdapter 通常只实现 IGPUWaterSurfaceSource。
CPU 与 GPU 版本统一的是 Shader 资源语义，不是内存所在地。
```

---

## 4. 统一 Shader 资源语义

第一版单层 FFT：

```text
set 0 binding 0  CameraUBO
set 0 binding 1  WaterParamsUBO
set 0 binding 2  FFTDisplacementMap
set 0 binding 3  FFTNormalAuxMap
set 0 binding 4  FrontParameterLUT
set 0 binding 5  BoreProfileDisplacement
set 0 binding 6  BoreProfileDerivative
set 0 binding 7  FoamDetailTexture
set 0 binding 8  LargeNormalTexture
set 0 binding 9  SmallNormalTexture
```

多 Cascade 正式版可选择：

```text
方案 A：每层独立 binding；实现直观，适合学习。
方案 B：descriptor array；更适合正式版。
```

无论 CPU 上传、GPU FFT 或 ExistingFFTAdapter，binding 2/3 或其 Cascade 数组的含义必须一致：

```text
Displacement RGBA:
R = horizontal displacement X
G = vertical height
B = horizontal displacement Z
A = Jacobian / breaking hint

NormalAux RGBA:
R = slope X
G = slope Z
B = dDxdx 或辅助导数
A = dDzdz 或辅助导数
```

---

## 5. 推荐代码目录

```text
src/
  scene/
    water/
      common/
        WaterSurfaceTypes.h
        WaterSurfaceComposer.h/.cpp

      sources/
        ICPUWaterSurfaceSource.h
        IGPUWaterSurfaceSource.h
        WSGerstnerCPU.h/.cpp
        WSTessendorfCPU.h/.cpp
        ExistingFFTAdapter.h/.cpp
        BoreFrontField.h/.cpp
        BoreWaveProfile.h/.cpp

      gpu/
        GPUFFT2D.h/.cpp
        WSTessendorfGPU.h/.cpp

      render/
        WaterVertex.h
        WaterGrid.h/.cpp
        WaterSurfaceMesh.h/.cpp
        DynamicImage2D.h/.cpp
        DepthBuffer.h/.cpp
        WaterQuadtreeAdapter.h/.cpp

      foam/
        FoamAnimator.h/.cpp
        FoamAdvection.h/.cpp

      optional_physics/
        WSShallowWater1D.h/.cpp
        WSShallowWater2D.h/.cpp

  main/
    water_grid/
    fft_cpu/
    fft_gpu/
    bore_mvp/
    water_final/

shaders/
  water/
    water.vert
    water.frag
    water_common.glsl
    bore_front.glsl
    bore_wave_profile.glsl
    foam_common.glsl

    fft/
      spectrum_init.comp
      spectrum_update.comp
      fft_stockham.comp
      fft_output.comp
```

---

## 6. 总阶段表

| 阶段 | 输出 | 第一版 Demo 是否必需 |
|---|---|---:|
| P0 | 接口审计、Stage 4 基线 | 是 |
| P1 | 每 swapchain image 独立 Depth + 固定 Grid | 是 |
| P2 | Gerstner 世界坐标验证 | 建议 |
| P3A | 单层 CPU Tessendorf FFT | 是，学习基准 |
| P3B | 多 Cascade CPU FFT | 最终 1 km 暴风雨版必需 |
| P4A | CPU FFT → Vulkan 动态浮点纹理 | 是，学习接入 |
| P4B | GPU Compute FFT | 正式自研版必需 |
| P5 | 有限长度 BoreFrontField + Front LUT | 是 |
| P6 | Profile 位移纹理 + 导数纹理 | 是 |
| P7 | FFT/Bore 几何合成 | 是 |
| P8 | 泡沫生成、三相位动画、后续状态型输运 | 是 |
| P9 | 水体光学与泥沙 | 是 |
| P10 | Quadtree/LOD 完整 1 km 验收 | 是 |
| P11 | 交互波适配 | 后置 |
| P12 | 暴风雨与频谱平滑过渡 | 是，最终画面版 |
| P13 | Profiling、格式与同步优化 | 是，交付版 |
| P14 | 可选 1D/2D SWE/Boussinesq | 否 |

---

# 7. P0：冻结 Stage 4 与接口审计

## P0.1 建立基线

```bat
git add .
git commit -m "Stage 4: complete texture descriptor and UBO pipeline"
git tag stage4-complete
git switch -c water-bore-v3-1
```

这里的“冻结”只表示保留可回退基线，不表示系统架构不再修订。

## P0.2 建立目标项目接口审计文档

创建：

```text
ExistingWaterSystemInterface.md
```

记录未来需要确认的事项：

```text
已有 FFT 是 CPU 还是 GPU Compute？
输出是 sampled image、storage image 还是 buffer？
displacement / normal / Jacobian 的格式？
FFT 有几层 Cascade？patch size 与频段？
世界坐标和 UV 如何映射？
交互波输出高度、速度还是法线？
四叉树在哪一层生成世界坐标？
每 tile 是否共享全局水体 descriptor？
是否使用 bindless？
一帧中 FFT、交互波、绘制的同步顺序？
```

## P0.3 验收

```text
[ ] stage4-complete tag 存在
[ ] CPU/GPU Source 接口拆分完成
[ ] Shader binding 契约写入代码注释与文档
[ ] 固定 Grid 被明确标记为验证模块
```

---

# 8. P1：深度缓冲与固定 3D Grid

## P1.1 深度格式查询

实现：

```cpp
VkFormat FindSupportedFormat(...);
VkFormat FindDepthFormat();
```

优先级：

```text
VK_FORMAT_D32_SFLOAT
VK_FORMAT_D32_SFLOAT_S8_UINT
VK_FORMAT_D24_UNORM_S8_UINT
```

## P1.2 每个 swapchain image 独立 DepthBuffer

禁止默认让所有 framebuffer 共享一张 depth image。

```cpp
std::vector<std::unique_ptr<DepthBuffer>> m_DepthBuffers;
```

数量：

```text
m_DepthBuffers.size() == swapchain image count
```

对应关系：

```text
Framebuffer 0 → ColorView 0 + DepthView 0
Framebuffer 1 → ColorView 1 + DepthView 1
Framebuffer 2 → ColorView 2 + DepthView 2
```

这可以避免多个 frame in flight 并行使用同一深度图产生读写冲突。

## P1.3 RenderPass 与 Pipeline

RenderPass 增加 depth attachment；Pipeline 增加：

```text
depthTestEnable  = VK_TRUE
depthWriteEnable = VK_TRUE
depthCompareOp   = VK_COMPARE_OP_LESS
```

## P1.4 固定 Grid

```text
129 × 129 vertices
128 × 128 quads
256 m × 256 m
XZ 平面
```

顶点：

```cpp
struct WaterVertex
{
    glm::vec3 position;
    glm::vec2 uv;
};
```

## P1.5 世界坐标 Debug

Fragment Shader 输出：

```text
世界坐标棋盘
X/Z 分量伪彩色
Tile 边界
```

相机移动时纹理不能“粘在相机上”或随着局部 UV 漂移。

## P1.6 Resize

```text
等待相关 GPU 工作结束
销毁 framebuffers
销毁全部 depth buffers
重建 swapchain views
按 swapchain image count 重建 depth buffers
重建 framebuffers
```

## P1 验收

```text
[ ] 每个 framebuffer 有独立 depth image
[ ] 深度遮挡正确
[ ] 线框拓扑正确
[ ] 世界坐标稳定
[ ] resize / minimize / restore 正常
[ ] Validation Layer 零错误
```

---

# 9. P2：Gerstner 临时验证层

P2 不替代 FFT，只用于快速验证 3D 水面数据路径。

## P2.1 4～8 条波

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

## P2.2 验证内容

```text
世界坐标位移
水平 + 垂直位移
解析坡度
相机移动稳定性
WaterSurfaceComposer 接口
```

## P2 验收

```text
[ ] Grid 出现连续动态波
[ ] 位移和法线一致
[ ] 波相位使用世界坐标
[ ] 可以无改 Shader 地替换为 FFT resource
```

---

# 10. P3A：单层 CPU Tessendorf FFT

## P3A.1 参数

学习阶段：

```cpp
resolution  = 64;
patchLength = 256.0f;
```

这只用于掌握算法和验证 GPU 结果，不代表最终 1 km 海面配置。

## P3A.2 实现顺序

```text
1. 离散波矢量 k
2. 确定性高斯随机数
3. Phillips 谱
4. h0(k)
5. h(k,t)
6. 朴素 2D IDFT，N=16/32
7. FFTW IFFT
8. slope / displacement / Jacobian
9. 输出 PGM/CSV
10. CPU 单点采样
```

## P3A.3 验证

```text
同 seed、同时间可复现
朴素 IDFT 与 FFTW 近似一致
虚部残差接近 0
无 NaN / Inf
风向改变后主方向改变
```

---

# 11. P3B：多 Cascade CPU FFT

单层 256 m patch 在 1 km 水面上会明显重复，因此最终暴风雨海面使用三层 Cascade。

## P3B.1 建议尺度

```text
Cascade 0：64 m   近景短波
Cascade 1：256 m  中尺度风浪
Cascade 2：1024 m 远景长涌浪
```

分辨率可先统一 128，再根据性能调整。第一版一线潮 MVP 可只接 Cascade 1；完整 1 km 暴风雨版本必须验证三层。

## P3B.2 频率分带

禁止三个完整 Phillips 谱直接相加。每层定义平滑频带：

```text
Cascade 0：高 k
Cascade 1：中 k
Cascade 2：低 k
```

使用平滑 band window：

```cpp
float bandWeight = LowCut(k) * HighCut(k);
P_cascade(k) = P_global(k) * bandWeight;
```

相邻层允许小范围平滑重叠，但总能量权重应接近 1，避免重复注入同一频率能量。

## P3B.3 随机场

各 Cascade 使用不同但固定 seed：

```text
seed + 0
seed + 101
seed + 211
```

避免不同尺度出现明显相位复制。

## P3B.4 CPU 输出

每层独立输出：

```text
displacement
normalAux
patchLength
resolution
frequency band
```

Shader 采样时按世界坐标和各自 patchLength 生成 UV，再合并 displacement 与 slope。

## P3B 验收

```text
[ ] 1 km 俯视 Debug 中无明显 256 m 周期重复
[ ] 三层频谱不存在明显能量重复
[ ] 关闭任意 Cascade 后，消失的是对应尺度细节
[ ] 大风参数下长涌浪由 Cascade 2 主导
```

---

# 12. P4A：CPU FFT 接入 Vulkan

## P4A.1 DynamicImage2D

支持：

```text
RGBA32F（学习版）
TRANSFER_DST
SAMPLED
每帧更新
```

第一版纹理：

```text
FFTDisplacementMap
FFTNormalAuxMap
```

## P4A.2 正确同步方案

### 学习验证方案 A：单套目标纹理

如果所有 frame 共享同一目标 image，仅等待“当前 frame slot”的 fence 并不足以保证其他飞行帧已经停止读取该 image。

安全做法只能是：

```text
等待所有可能引用该 image 的 in-flight fences
或 vkQueueWaitIdle / vkDeviceWaitIdle
→ 上传
→ barrier
→ 绘制
```

此方案只用于早期正确性验证。

### 正式方案 B：每 frame slot 一套目标纹理

```text
Frame 0: displacement0 / normalAux0
Frame 1: displacement1 / normalAux1
Frame 2: displacement2 / normalAux2
```

流程：

```text
等待当前 frame slot fence
→ 更新该 slot staging buffer
→ 更新该 slot image
→ 设置该 slot descriptor
→ 本帧采样该 slot image
```

## P4A.3 Image Barrier

上传前：

```text
srcStage  = VERTEX_SHADER | FRAGMENT_SHADER
srcAccess = SHADER_READ

dstStage  = TRANSFER
dstAccess = TRANSFER_WRITE
```

上传后：

```text
srcStage  = TRANSFER
srcAccess = TRANSFER_WRITE

dstStage  = VERTEX_SHADER | FRAGMENT_SHADER
dstAccess = SHADER_READ
```

如果 `normalAux` 仅在顶点阶段采样，可只使用 Vertex Shader stage；只要片元阶段也采样，就必须包含 Fragment Shader。

## P4A.4 验收

```text
[ ] CPU FFT 动态驱动 Grid
[ ] 多帧运行无同步 Validation 报错
[ ] 每帧资源与 descriptor 对应正确
[ ] 单套 image 方案明确只用于验证
```

---

# 13. P4B：GPU Compute FFT

这是正式自研版本必须包含的阶段。

## P4B.1 文件

```text
src/scene/water/gpu/
  GPUFFT2D.h/.cpp
  WSTessendorfGPU.h/.cpp

shaders/water/fft/
  spectrum_init.comp
  spectrum_update.comp
  fft_stockham.comp
  fft_output.comp
```

## P4B.2 资源

```text
H0 / H0MinusConj
Frequency / WaveVector metadata
SpectrumPing
SpectrumPong
FFT intermediate Ping/Pong
Displacement Storage Image
NormalAux Storage Image
```

第一版使用 `RGBA32F` 或 buffer 中的 `vec2` 复数，正确后再评估 `RGBA16F`。

## P4B.3 推荐迁移顺序

### 步骤 1：CPU 生成 h0，上传一次

为了保证 CPU/GPU 基准完全一致，第一版不要立即在 GPU 上重新实现随机数。

```text
WSTessendorfCPU 生成 h0
→ 一次性上传 GPU
→ GPU 负责时间演化、FFT 与输出
```

这样可以隔离随机数实现差异。

### 步骤 2：`spectrum_update.comp`

每帧计算：

```text
h(k,t)
slope spectra
displacement spectra
derivative spectra
```

### 步骤 3：Stockham 横向 FFT

对每一行执行 1D Stockham IFFT，按 stage ping-pong。

### 步骤 4：Stockham 纵向 FFT

对每一列执行 1D Stockham IFFT。

### 步骤 5：`fft_output.comp`

```text
除以 N×N
应用必要的索引/符号约定
组装 displacement
组装 slope / Jacobian
写 Storage Image
```

### 步骤 6：Compute → Graphics Barrier

```text
srcStage  = COMPUTE_SHADER
srcAccess = SHADER_WRITE

dstStage  = VERTEX_SHADER | FRAGMENT_SHADER
dstAccess = SHADER_READ
```

### 步骤 7：数值对比

临时将 GPU 输出 readback 到 staging buffer：

```text
同 seed
同参数
同时间
CPU 与 GPU displacement / slope 比较
```

检查：

```text
max absolute error
mean absolute error
NaN count
```

readback 仅用于测试，不进入正式每帧路径。

### 步骤 8：GPU `spectrum_init.comp`

CPU h0 上传版本完全通过后，再实现 GPU 初始化。若要求与 CPU 精确对应，应采用共享的预生成高斯表或可严格复现的哈希规范；否则只要求统计分布一致，不要求逐元素相同。

## P4B.4 多 Cascade GPU FFT

每层可独立调度，也可使用 z 维/descriptor array 批量处理。第一版建议每层独立，便于调试。

```text
for each cascade:
  update spectrum
  row FFT
  column FFT
  output
```

## P4B.5 验收

```text
[ ] GPU FFT 与 CPU FFT 在相同 h0、相同时间下近似一致
[ ] 不再每帧 CPU → GPU 上传 FFT 数据
[ ] Storage Image 可被 Water Shader 直接采样
[ ] 输出 binding 语义与 CPU 上传版完全相同
[ ] 单层完成后可扩展到三 Cascade
[ ] CPU FFT 继续保留为自动测试基准
```

---

# 14. P5：有限长度 BoreFrontField

## P5.1 世界坐标

```text
n = 基础推进法向
t = 沿前沿切向
p = worldXZ

a = dot(p - origin, t)
ξ = dot(p - origin, n)
frontU = a / frontLength + 0.5
```

Front LUT 采样坐标：

```glsl
float frontUClamped = clamp(frontU, 0.0, 1.0);
```

潮头位置：

```text
ξ_front = speed × time + offsetCurve(frontU)
s = ξ - ξ_front
```

## P5.2 有限长度 Mask

```glsl
float edgeFade = 0.03;

float lengthMask =
    smoothstep(0.0, edgeFade, frontU) *
    (1.0 - smoothstep(1.0 - edgeFade, 1.0, frontU));
```

应用到：

```text
bore displacement
foam source
water rise
flow velocity
```

第一版使用 lengthMask；正式版增加：

```text
Bore Active Region Mask
或河道/区域 SDF
```

防止两端以规则矩形方式消失。

## P5.3 Front LUT 通道

```text
R = front offset
G = crest amplitude multiplier
B = foam multiplier
A = profile phase offset
```

Sampler：

```text
clamp-to-edge
linear
no mip
```

## P5.4 LUT 导数与世界单位换算

若 LUT 中存的是对纹理 U 的导数 `df/du`，则：

```text
da/du = frontLength

df/da = (df/du) / frontLength
```

局部 signed-distance 梯度：

```text
∇s = n - (df/da)t
```

局部前沿法向：

```cpp
localFrontNormal = normalize(n - derivativeWorld * t);
```

忽略 `1/frontLength` 会导致 1 km 前沿法向严重错误。

## P5.5 M3 局部验收

固定 Grid 只有 256 m，因此本阶段只验收：

```text
数学定义长度为 1 km
固定 Grid 可看到其中一个 256 m 局部片段
世界坐标、LUT、局部法向与相位连续
可选低密度俯视 Debug 查看完整 1 km
```

完整 1 km 几何质量与性能留到 P10。

---

# 15. P6：Bore Wave Profile 位移与导数

## P6.1 两张纹理

### Profile Displacement：`RGBA16F`

```text
R = forward displacement
G = upward displacement
B = foam source
A = crest mask
```

### Profile Derivative：`RGBA16F`

```text
R = dForward / ds
G = dUpward / ds
B = flow speed
A = breaking weight
```

这样 `foamSource` 与 `crestMask` 完全分离，后方泡沫不会继续压低 FFT。

## P6.2 UV

```text
U = signed distance 映射到 profileWidth
V = profile animation phase
```

```glsl
float profileU =
    clamp(s / profileWidth * 0.5 + 0.5, 0.0, 1.0);
```

## P6.3 Profile 动画模式

潮头前进已经由 `speed × time` 控制，`profileV` 不能再次让整个潮头平移。

支持两种模式：

```text
LoopingProfile：
用于持续近岸破浪和局部翻卷循环。

OneShotProfile：
用于潮涌从形成、抬升、翻卷到破碎的一次性事件。
```

一线潮默认优先 `OneShotProfile`，或使用非常缓慢、只改变翻卷形态的局部循环。

Sampler：

```text
U clamp-to-edge
V repeat（Looping）或 clamp-to-edge（OneShot）
linear
no mip 或 textureLod(..., 0)
```

## P6.4 位移

```glsl
vec4 profile = texture(profileDisplacement, profileUV);

vec2 horizontal =
    localFrontNormal * profile.r;

float vertical =
    profile.g + waterRise;
```

最终乘：

```text
lengthMask
Front LUT amplitude multiplier
Bore active region mask
```

## P6.5 法线坡度

```glsl
vec4 deriv = texture(profileDerivative, profileUV);

vec2 boreSlope =
    deriv.g * localFrontNormal;
```

第一版只使用垂直位移导数。第二版再把 `dForward/ds` 纳入参数化曲面的切线修正。

## P6.6 Profile 生成

```text
多条横截面曲线
→ 不同动画帧
→ 烘焙位移纹理
→ 对距离轴做解析或数值导数
→ 烘焙导数纹理
```

导数纹理必须与位移纹理使用相同距离单位；若烘焙的是 normalized U 导数，生成阶段就换算成世界空间每米导数。

## P6 验收

```text
[ ] 潮头不再只是 Gaussian 峰
[ ] forward/upward displacement 连续
[ ] boreSlope 来源明确且方向正确
[ ] foamSource 和 crestMask 独立
[ ] OneShot/Looping 模式行为符合定义
```

---

# 16. P7：FFT 与 Bore 的几何合成

## P7.1 FFT suppression

```glsl
float crestMask = profile.a * lengthMask;

float fftWeight =
    mix(1.0, crestFFTScale, crestMask);
```

推荐：

```text
crestFFTScale = 0.2 ～ 0.6
```

禁止使用 `profile.b` 泡沫通道作为 suppression mask。

## P7.2 位移组合

```glsl
worldPosition.xz +=
    fftHorizontal * fftWeight
  + boreHorizontal;

worldPosition.y +=
    fftHeight * fftWeight
  + boreVertical;
```

多 Cascade：

```text
先合成各 Cascade FFT 位移与坡度
再施加 crest suppression
再叠加 Bore
```

也可以只抑制中/高频 Cascade，保留低频长涌浪；此方式通常比压低全部 FFT 更自然，作为 P7 优化项。

## P7.3 坡度组合

```glsl
vec2 totalSlope =
    fftSlope * fftWeight
  + boreSlope
  + detailSlope;

vec3 normal = normalize(vec3(
    -totalSlope.x,
     1.0,
    -totalSlope.y));
```

禁止直接线性相加两个单位法线。

## P7 验收

```text
[ ] FFT 不切断连续潮头
[ ] 后方泡沫区不会无故压平 FFT
[ ] 潮头坡度和局部曲线法向一致
[ ] 多 Cascade 开关不影响 Bore 世界坐标
```

---

# 17. P8：泡沫系统

## P8.1 泡沫源与外观分离

```text
Foam Source：哪里产生泡沫
Foam Appearance：泡沫纹理、细胞形态、法线和颜色
```

泡沫源：

```glsl
float foamSource = max(
    profile.b,
    slopeFoam,
    fftJacobianFoam);

foamSource *= lengthMask;
```

## P8.2 三相位平滑滚动

```text
phase 0
phase 1/3
phase 2/3
```

沿 `flowVelocity` 对 Foam Detail Texture 进行世界坐标采样，再以周期权重混合，降低单张滚动纹理循环跳变。

```glsl
FoamDetail SampleAdvectedFoam(
    vec2 worldXZ,
    vec2 velocity,
    float time);
```

## P8.3 状态型泡沫（第二版）

Ping-Pong Foam Texture：

```text
F_next(p)
= F_prev(p - velocity × dt)
+ source × dt
- decay × F_prev × dt
+ diffusion
```

第一版可以 CPU 或 Compute 半拉格朗日；状态型泡沫不阻塞三相位视觉版本。

## P8.4 材质影响

泡沫至少修改：

```text
BaseColor
Roughness
Specular
Normal
Opacity / coverage
Lightning response
```

## P8 验收

```text
[ ] 白浪不是静态白线
[ ] 泡沫在潮头最强并向后衰减
[ ] 泡沫沿 flow 方向移动
[ ] 纹理循环无明显跳变
[ ] 两端受 length/active mask 控制
```

---

# 18. P9：水体 Shader

按以下顺序实现，禁止同时堆叠所有效果：

```text
1. Debug displacement
2. Debug total slope / normal
3. Debug foam source
4. 基础 Fresnel
5. 天空反射和高光
6. 深度/距离水色
7. 吸收与散射
8. 泥沙浑浊
9. 泡沫材质
10. 雾与远景融合
```

建议材质参数：

```text
waterBodyColor
sedimentColor
absorptionRGB
scatteringRGB
roughness
foamColor
foamRoughness
foamNormalStrength
stormReflectionStrength
```

数据纹理 sampler：

```text
FFT Texture:
repeat
linear
可使用 mip 或显式 LOD

Front LUT:
clamp-to-edge
linear
no mip

Wave Profile:
U clamp-to-edge
V repeat / clamp 取决于模式
linear
no mip 或显式 LOD 0
```

---

# 19. P10：Quadtree/LOD 与完整 1 km 验收

## P10.1 接入原则

不重写第二套生产四叉树。自研固定 Grid 验证通过后，实现：

```text
WaterQuadtreeAdapter
```

它只负责把相同世界坐标函数、FFT resources、Bore UBO 和 Profile textures 绑定到目标项目式 tile 渲染入口。

## P10.2 Bore-aware LOD

LOD 评分增加：

```text
相机距离
屏幕空间误差
到潮头距离 abs(s)
crestMask
局部几何坡度
```

潮头附近强制提高 LOD。

## P10.3 裂缝处理

第一版优先：

```text
skirts
```

后续：

```text
stitching indices
edge morphing
共享边界
```

## P10.4 完整 1 km 验收

```text
[ ] 能完整查看约 1 km 前沿
[ ] Front LUT 跨 tile 连续
[ ] Wave Profile 相位跨 tile 连续
[ ] 两端 active mask 自然
[ ] Tile 切换不导致泡沫漂移
[ ] 潮头附近几何精度足够
[ ] 三 Cascade 下无明显周期重复
[ ] 性能达到目标预算
```

这才是完整 1 km 验收；P5/P6 固定 Grid 只验收局部片段。

---

# 20. P11：交互波适配（后置）

目标项目已有波动方程，因此产品路线只实现：

```text
ExistingInteractionAdapter
```

统一输出：

```text
interaction height
interaction slope
interaction mask
可选 velocity
```

组合：

```glsl
finalHeight += interactionHeight * interactionWeight;
finalSlope  += interactionSlope  * interactionWeight;
```

潮头主几何区可以降低交互权重，避免局部波纹破坏一线潮轮廓。

自研学习项目可在这一阶段后补离散波动方程，但不能阻塞 FFT + Bore + Shader 主线。

---

# 21. P12：暴风雨与频谱平滑过渡

## P12.1 固定暴风雨参数先完成

第一版使用固定：

```text
wind speed
wind direction
spectrum amplitude
foam strength
water roughness
fog density
```

不要在主链路未稳定时实现动态天气过渡。

## P12.2 禁止瞬间重建 h0

直接改变 windSpeed 并重新生成 `h0(k)` 会导致海面随机场跳变。

正式版本使用两个频谱状态：

```text
Spectrum A：当前风况
Spectrum B：目标风况
```

交叉淡化：

```text
h(k,t) = (1-w) hA(k,t) + w hB(k,t)
```

`w` 在数秒到数十秒内平滑变化。

对多 Cascade，所有 Cascade 同步过渡，但可按尺度设置不同响应时间：

```text
短波响应快
长涌浪响应慢
```

## P12.3 闪电

闪电影响：

```text
天空亮度
水面镜面反射
泡沫亮度
雾散射
```

采用短脉冲曲线，不修改 FFT 频谱。

## P12 验收

```text
[ ] 天气切换不导致海面瞬间跳变
[ ] 短波和长波具有合理响应时间差
[ ] 闪电同步照亮水面、泡沫和雾
```

---

# 22. P13：性能、同步与格式优化

## P13.1 学习版

```text
N = 64 / 128
CPU FFT
RGBA32F
queue/device idle 保证正确
```

## P13.2 中间优化版

```text
per-frame images
持续映射 ring staging
明确 image barrier
RGBA16F displacement/normal
异步 transfer
```

## P13.3 正式版

```text
GPU FFT Compute
Storage Image 直接输出
无每帧 CPU FFT 上传
多 Cascade
Existing GPU FFT adapter 可直接替换
```

## P13.4 推荐纹理格式

| 数据 | 验证格式 | 正式建议 |
|---|---|---|
| FFT displacement | RGBA32F | RGBA16F |
| FFT normalAux | RGBA32F | RGBA16F |
| Bore Profile displacement | RGBA16F | RGBA16F |
| Bore Profile derivative | RGBA16F | RGBA16F |
| Front LUT | RGBA32F | RGBA16F/RGBA32F |
| Interaction height | R32F | R16F |
| Flow | RG32F | RG16F |
| Foam source/state | R16F | R8/R16F |
| SWE state | RGBA32F | RGBA32F |

## P13.5 Profiling 点

```text
CPU FFT update
CPU upload
GPU spectrum update
GPU row FFT
GPU column FFT
GPU output pass
Water vertex stage
Water fragment stage
Foam compute
Quadtree tile count
```

---

# 23. P14：可选物理升级

## P14.1 1D SWE

只在需要以下能力时实施：

```text
潮头高度由水位差和流速产生
可解释的速度场
更可信的潮后水位
横截面质量守恒
```

第一版：

```text
Finite Volume
Rusanov/HLL
CFL
positivity
Dam Break
Moving Bore
```

通过后升级：

```text
MUSCL
minmod / MC limiter
SSP-RK2
```

1D SWE 的 `eta/u` 可替换 Profile 的几何主剖面，但 Wave Profile 仍可作为破浪外观层。

## P14.2 2D SWE/Boussinesq

只在明确出现以下需求时进入：

```text
河道地形显著改变潮头
绕障碍传播
二维汇流
潮头断裂与重组
明显 undular bore
```

不属于第一版执行路径。

---

# 24. 修正后的里程碑

## M0：Stage 4 基线

```text
现有 textured quad 稳定
CPU/GPU Source 接口定义完成
```

## M1：3D 水面基础

```text
每 framebuffer 独立 depth
固定 Grid
世界坐标 Debug
```

## M2：海浪数据链

```text
Gerstner
单层 CPU FFT
CPU 动态纹理接入
```

## M3：一线潮局部 MVP

固定 256 m Grid 上验证：

```text
数学 frontLength = 1 km
看到其中 256 m 局部片段
Front LUT 连续
Wave Profile 位移与导数正确
纯色 foam source 正确
```

不在此阶段宣称完整 1 km 几何验收。

## M4：GPU FFT

```text
GPU 与 CPU 参考近似一致
不再每帧上传 FFT 数据
```

## M5：视觉完成版

```text
Bore + FFT
泡沫动画
水体光学
泥沙与暴风雨
```

## M6：完整 1 km 验收

```text
接 Quadtree/LOD
完整查看 1 km 潮头
跨 tile 连续
三 Cascade
性能达标
```

## M7：项目适配

```text
ExistingFFTAdapter
ExistingInteractionAdapter
ExistingQuadtreeAdapter
```

---

# 25. 推荐实施顺序与提交粒度

## 第一批：现在立即执行

```text
1. 建立 v3.1 分支与接口头文件
2. 每 swapchain image 独立 DepthBuffer
3. 固定 256 m Grid
4. 世界坐标 Debug
5. Gerstner 验证
```

建议提交：

```text
P1.1 add per-swapchain depth resources
P1.2 add fixed XZ water grid
P1.3 add world-space debug shader
P2.1 add Gerstner reference source
```

## 第二批：CPU FFT

```text
1. 单层 CPU FFT
2. naive IDFT 与 FFTW 对比
3. 动态浮点纹理接入
4. 三 Cascade CPU 结构
```

## 第三批：一线潮局部 MVP

```text
1. finite BoreFrontField
2. Front LUT + derivative scaling
3. Profile displacement texture
4. Profile derivative texture
5. crestMask suppression
6. foam source debug
```

## 第四批：GPU FFT 与泡沫

```text
1. GPU spectrum update
2. Stockham row/column
3. output images
4. CPU/GPU test
5. three-phase foam
6. water shader
```

## 第五批：完整范围与项目适配

```text
1. Quadtree adapter
2. full 1 km test
3. three cascades
4. storm transition
5. interaction adapter
6. profiling
```

---

# 26. 最终完成标准

## 算法

```text
[ ] CPU 单层 FFT 正确
[ ] CPU 多 Cascade 频段分离
[ ] GPU Stockham FFT 与 CPU 参考近似一致
[ ] CPU FFT 保留为自动测试
```

## Vulkan

```text
[ ] 每 swapchain image 独立 DepthBuffer
[ ] CPU 动态纹理同步正确
[ ] GPU Compute → Graphics barrier 正确
[ ] GPU FFT 不需要每帧 CPU 上传
```

## 一线潮

```text
[ ] finite frontLength 生效
[ ] Front LUT 坐标与世界导数换算正确
[ ] Profile displacement / derivative 双纹理
[ ] foamSource 与 crestMask 分离
[ ] OneShot/Looping 动画模式明确
[ ] 潮后水位抬升
```

## 视觉

```text
[ ] FFT 背景不破坏连续潮头
[ ] 泡沫不再是静态白线
[ ] 水色、泥沙、Fresnel 和暴风雨成立
[ ] 动态天气不会瞬间重置海面随机场
```

## 1 km 与工程适配

```text
[ ] 固定 Grid 完成局部 MVP
[ ] Quadtree/LOD 完成完整 1 km 验收
[ ] 三 Cascade 降低海面周期重复
[ ] Existing FFT 可通过 GPU Adapter 替换自研 FFT
[ ] 交互波作为后置 Adapter 接入
```

---

# 27. 当前下一步

当前只执行 P1 和 P2，不提前写 FFT、Wave Profile 或泡沫：

```text
Step 1：把 DepthBuffer 改成每个 swapchain image 一份。
Step 2：RenderPass / Framebuffer / resize 全部适配 depth vector。
Step 3：生成 129×129 的 256 m XZ Grid。
Step 4：加入世界坐标 Debug Shader 和线框模式。
Step 5：用 4～8 条 Gerstner 波验证世界坐标位移与法线。
```

完成这一批后的交付文件应包括：

```text
DepthBuffer.h/.cpp
SwapChain 或 framebuffer 深度资源改动
WaterVertex.h
WaterGrid.h/.cpp
WaterGridApp.h/.cpp
water_debug.vert
water_debug.frag
WSGerstnerCPU.h/.cpp
```

验收通过后，再进入 P3A 单层 CPU Tessendorf FFT。
