#version 450

// Patch 顶点只存局部坐标和 skirt 标志
layout(location = 0) in vec2 inLocalXZ;
layout(location = 1) in float inSkirt;

layout(set = 0, binding = 0) uniform CameraUBO
{
    mat4 model;
    mat4 view;
    mat4 projection;
    vec4 cameraWorldPosition;
    ivec4 debug;
} camera;

struct WaterTileGPU
{
    vec4 originSize;
    uvec4 metadata;
};

// GLSL 中有两种主要的缓冲区内存布局规则：
    // std140：主要用于 Uniform Buffer（UBO），规则很严格，会产生较多填充字节。
    // 例如，vec3 会被对齐到 vec4 的边界，float 会被对齐到 16 字节。
    // std430：专门为 Shader Storage Buffer（SSBO）设计，布局规则比 std140 更自然、更紧凑。
    // 在 std430 下，基本类型的对齐规则更宽松（如 vec3 依然占 12 字节，float 占 4 字节，不再强制 16 字节对齐）。
// 虽然 SSBO 默认布局就是 std430，但显式写出 std430 可以让你在 C++ 端定义对应的 WaterTileGPU 结构体时，
// 更容易按 GLSL 的实际规则进行对齐，避免因为隐式规则不同导致的数据错位。
// 对于你存有成百上千个 Tile 实例的 SSBO 来说，紧凑的内存就意味着更小的带宽消耗和更少的缓存压力，
// 这对于逐顶点都要读取一次的实例数据非常关键。
// readonly：性能提示与错误预防 对于实例化绘制中大量顶点同时读取同一个 Tile 数据的情况，只读缓存命中率会更高
layout(std430, set = 0, binding = 14) readonly buffer WaterTileBuffer
{
    WaterTileGPU tiles[];
} tileBuffer;

// ===== domain：河流场纹理的世界空间范围与河流总长度 =====
// x = worldMinX    – 河流场纹理左下角 X 坐标（米）
// y = worldMinZ    – 河流场纹理左下角 Z 坐标（米）
// z = worldSize    – 河流场纹理的边长（米）
// w = riverLength  – 河流中轴线总长度（米）

// ===== bore：涌潮波前的沿河进度与曲率控制 =====
// x = boreProgressMeters   – 涌潮从入海口沿河流中轴线前进的总距离（米）
//                           由 initialOffset + speed * time 计算，驱动潮头在河道中推进
// y = riverBoreCurvatureMeters – 弯曲波前线的曲率影响范围（米），用于控制波前形变
// z = amplitudeScale        – 河流涌潮的全局振幅缩放（1.0 = 标准强度）
// w = decayRate             – 涌潮沿河道的振幅衰减速率（每米衰减比例）
layout(set = 0, binding = 15) uniform RiverFieldUBO
{
    vec4 domain;
    vec4 bore;
} river;

// 这是一个组合图像采样器的声明。在 Vulkan 中，它对应描述符类型 VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER。
// GPU 硬件把 sampler2D 理解为一个“采样器 + 图像”的逻辑对象。
// 当你调用 texture(riverFlowTexture, uv) 时，GPU 会利用已绑定的采样器和图像视图，
// 根据 UV 坐标自动完成寻址、滤波和格式转换。

// 从概念上，你可以把它理解为一个 二维的 vec4 数组。
// 它是一张二维纹理，坐标用 (u, v) 索引，其中 u 和 v 的范围是 [0, 1]。
// 每次采样返回一个 vec4，对应存储的 RGBA 四个通道。

// 但硬件采样不同于 C++ 数组的精确下标访问——它会根据采样器设置自动进行双线性插值或最近邻查询，返回的是滤波后的值。
// 此外，GPU 的纹理缓存和布局（如 Tiling、Swizzle）与 CPU 的线性数组在存储结构上也完全不同。
// sampler2D 本身并不决定纹理是否有 mipmap。Mipmap 是在创建 VkImage 时通过 mipLevels 指定的

// ===== Flow Map 各通道 =====
// R = 流向 X 分量
// G = 流向 Z 分量
// B = 涌潮振幅缩放系数
// A = 水域掩码

// ===== Coordinate Map 各通道 =====
// R = 归一化进度
// G = 归一化横向坐标
// B = 归一化河岸距离
// A = 曲率权重
layout(set = 0, binding = 16) uniform sampler2D riverFlowTexture;
layout(set = 0, binding = 17) uniform sampler2D riverCoordinateTexture;
layout(set = 0, binding = 20) uniform sampler2D riverProgressTexture;
layout(set = 0, binding = 21) uniform sampler2D shoreMaskTexture;

struct BoreEventGPU
{
    vec4 motion;
    vec4 shape;
    vec4 appearance;
    vec4 suppression;
};

layout(set = 0, binding = 18) uniform MultiBoreUBO
{
    ivec4 metadata;
    vec4 riverInfo;
    vec4 crestNoiseA;   // x=横向频率 y=沿河频率X z=沿河频率Y w=动画速度
    vec4 crestNoiseB;   // x=细节频率 y=细节权重 z=振幅下限 w=振幅上限
    vec4 crestNoiseC;   // x=顶抖强度 y=顶抖频率
} multiBore;

layout(std430, set = 0, binding = 19) readonly buffer BoreEventBuffer
{
    BoreEventGPU events[];
} boreEvents;

layout(set = 0, binding = 1) uniform WaterParamsUBO
{
    vec4 patchLengths;
    vec4 amplitudeScales;
    ivec4 metadata;
    vec4 simulation;
} water;

layout(set = 0, binding = 2) uniform sampler2D fftDisplacement0;
layout(set = 0, binding = 3) uniform sampler2D fftNormalAux0;

layout(set = 0, binding = 4) uniform sampler2D fftDisplacement1;
layout(set = 0, binding = 5) uniform sampler2D fftNormalAux1;

layout(set = 0, binding = 6) uniform sampler2D fftDisplacement2;
layout(set = 0, binding = 7) uniform sampler2D fftNormalAux2;

layout(set = 0, binding = 8) uniform BoreFrontUBO
{
    vec4 originSpeedTime;
    vec4 directionLengthFade;
    vec4 motionDebug;
    vec4 lutInfo;
} bore;

layout(set = 0, binding = 9) uniform sampler2D frontParameterLUT;
layout(set = 0, binding = 10) uniform sampler2D frontDerivativeLUT;

layout(set = 0, binding = 11) uniform BoreProfileUBO
{
    vec4 domain;
    vec4 animation;
    vec4 geometry;
    vec4 suppression;
} profileConfig;

layout(set = 0, binding = 12) uniform sampler2D boreProfileDisplacement;
layout(set = 0, binding = 13) uniform sampler2D boreProfileDerivative;

layout(set = 1, binding = 0) uniform FoamParamsUBO
{
    vec4 animation;
    vec4 sourceStrength;
    vec4 thresholds;
    vec4 appearance;
    vec4 state;
    vec4 runtime;
    vec4 domain;
} foam;

layout(location = 0) out vec3 fragWorldPosition;
layout(location = 1) out vec3 fragWorldNormal;
layout(location = 2) out vec2 fragUV;
layout(location = 3) out vec4 fragDisplacement;
layout(location = 4) out vec4 fragNormalAux;
layout(location = 5) out vec4 fragBoreDebug0;
layout(location = 6) out vec4 fragBoreDebug1;
layout(location = 7) out vec4 fragBoreProfile0;
layout(location = 8) out vec4 fragBoreProfile1;
layout(location = 9) out vec4 fragComposition;
layout(location = 10) out vec4 fragSlopeDebug;
layout(location = 11) out vec4 fragFoamSourceData;
layout(location = 12) out vec4 fragFoamFlowData;
layout(location = 13) out vec4 fragFinalDisplacement;
layout(location = 14) out vec4 fragRiverFlow;
layout(location = 15) out vec4 fragRiverCoord;
layout(location = 16) out vec4 fragShore;

struct CascadeSample
{
    vec3 displacement;
    vec2 slope;
    float breaking;
};

CascadeSample SampleCascade(
    sampler2D displacementTexture,
    sampler2D normalAuxTexture,
    vec2 worldXZ,
    float patchLength,
    float amplitudeScale
){
    vec2 uv =
        fract(worldXZ / patchLength);

    vec4 displacement =
        texture(displacementTexture, uv);

    vec4 normalAux =
        texture(normalAuxTexture, uv);

    CascadeSample result;
    result.displacement =
        displacement.xyz *
        amplitudeScale;

    result.slope =
        normalAux.xy *
        amplitudeScale;

    result.breaking =
        clamp(
            1.0 - displacement.a,
            0.0,
            1.0
        );

    return result;
}

float SmoothStepDerivative(
    float edge0,
    float edge1,
    float x
){
    if(x <= edge0 || x >= edge1){
        return 0.0;
    }

    float t =
        (x - edge0) /
        (edge1 - edge0);

    return
        6.0 *
        t *
        (1.0 - t) /
        (edge1 - edge0);
}

float Hash21(vec2 p){
    p = fract(p * vec2(123.34, 456.21));
    p += dot(p, p + 45.32);
    return fract(p.x * p.y);
}

float ValueNoise(vec2 p){
    vec2 i = floor(p);
    vec2 f = fract(p);
    vec2 u = f * f * (3.0 - 2.0 * f);
    float a = Hash21(i);
    float b = Hash21(i + vec2(1.0, 0.0));
    float c = Hash21(i + vec2(0.0, 1.0));
    float d = Hash21(i + vec2(1.0, 1.0));
    return mix(mix(a, b, u.x), mix(c, d, u.x), u.y);
}

// 多层分形噪声：叠加 5 个倍频，得到大起伏 + 细碎复杂波动，返回约 [0,1]
float FBM(vec2 p){
    float sum = 0.0;
    float amp = 0.5;
    float norm = 0.0;
    for(int i = 0; i < 5; ++i){
        sum += amp * ValueNoise(p);
        norm += amp;
        p *= 2.02;      // 略偏离 2.0，避免各层网格对齐产生规则伪影
        amp *= 0.5;
    }
    return sum / norm;
}

// 低频 FBM：只叠 2 层，最高频约为基频的 2 倍。
// 用于振幅/水位这类必须平滑的量——5 层倍频会把最细层推到亚米级，
// 在数米顶点间距的水面网格上必然走样成细纹。
float FBM2(vec2 p){
    float sum = 0.0;
    float amp = 0.5;
    float norm = 0.0;
    for(int i = 0; i < 2; ++i){
        sum += amp * ValueNoise(p);
        norm += amp;
        p *= 2.02;
        amp *= 0.5;
    }
    return sum / norm;
}

void main(){
    // 将顶点从模型空间变换到世界空间，得到未变形的水面基础位置
    // Patch 局部坐标映射到 Tile 世界坐标
    
    // gl_InstanceIndex 扮演了一个“钥匙”的角色。它把顶点批次的编号和你在 SSBO 中存储的 Tile 数据数组联系了起来
    WaterTileGPU tile =
        tileBuffer.tiles[gl_InstanceIndex];

    vec2 baseWorldXZ =
        tile.originSize.xy +
        inLocalXZ *
        tile.originSize.z;

    vec3 baseWorldPosition =
        vec3(
            baseWorldXZ.x,
            0.0,
            baseWorldXZ.y
        );

    // 将顶点的世界坐标映射为河流场纹理的 UV
    vec2 riverUV =
        clamp(
            (baseWorldPosition.xz - river.domain.xy) /
                river.domain.z,
            vec2(0.0),
            vec2(1.0)
        );

    // 只采样 mipmap 的第 0 级，避免因 mipmap 生成而导致数据偏差
    // // 采样 Flow Map 和 Coordinate Map（强制 LOD 0，避免模糊）
    vec4 riverFlow =
        textureLod(
            riverFlowTexture,
            riverUV,
            0.0
        );

    vec4 riverCoord =
        textureLod(
            riverCoordinateTexture,
            riverUV,
            0.0
        );

    bool useProgressField = (multiBore.metadata.w == 1);
    vec4 progressField = textureLod(riverProgressTexture, riverUV, 0.0);

    // 为计算 progress 的梯度，获取相邻像素的 progress 值
    ivec2 coordinateSize =
        textureSize(
            riverCoordinateTexture,
            0
        );

    vec2 texelUV =
        1.0 / vec2(coordinateSize);

    float texelWorldSize =
        river.domain.z /
        float(coordinateSize.x);

    float progressLeft =
        (useProgressField
            ? textureLod(riverProgressTexture, riverUV - vec2(texelUV.x, 0.0), 0.0).r
            : textureLod(riverCoordinateTexture, riverUV - vec2(texelUV.x, 0.0), 0.0).r)
         * river.domain.w;

    float progressRight =
        (useProgressField
            ? textureLod(riverProgressTexture, riverUV + vec2(texelUV.x, 0.0), 0.0).r
            : textureLod(riverCoordinateTexture, riverUV + vec2(texelUV.x, 0.0), 0.0).r)
         * river.domain.w;
    
    float progressDown =
        (useProgressField
            ? textureLod(riverProgressTexture, riverUV - vec2(0.0, texelUV.y), 0.0).r
            : textureLod(riverCoordinateTexture, riverUV - vec2(0.0, texelUV.y), 0.0).r)
         * river.domain.w;

    float progressUp =
        (useProgressField
            ? textureLod(riverProgressTexture, riverUV + vec2(0.0, texelUV.y), 0.0).r
            : textureLod(riverCoordinateTexture, riverUV + vec2(0.0, texelUV.y), 0.0).r)
         * river.domain.w;

    // progress 的梯度（世界空间），指向 progress 增加最快的方向，即河流的切线方向
    vec2 progressGradient =
        vec2(
            progressRight - progressLeft,
            progressUp - progressDown
        ) /
        (2.0 * texelWorldSize);

    fragRiverFlow = riverFlow;
    fragRiverCoord = riverCoord;

    fragShore = textureLod(shoreMaskTexture, riverUV, 0.0);

    // riverFlow.rg 是从 Flow Map（绑定在 binding=16）中采样得到的局部流向，它随河道弯曲而变化
    // 优先使用梯度方向作为局部流向（更平滑），梯度过小时回退 Flow Map；
    // 若两者都近似零向量(河道末端/未烘焙)，用固定默认方向，避免 normalize(0)=NaN 造成尖锐凸起。
    vec2 localFlowDirection =
        length(progressGradient) > 1.0e-4
        ? normalize(progressGradient)
        : (length(riverFlow.rg) > 1.0e-4
            ? normalize(riverFlow.rg)
            : vec2(0.0, 1.0));

    // if(dot(localFlowDirection, riverFlow.rg) < 0.0){
    //     localFlowDirection =
    //         -localFlowDirection;
    // }

    // 顶点的沿河进度（米）：由归一化进度和河流总长度计算
    float progressMeters =
        (useProgressField ? progressField.r : riverCoord.r) 
        * river.domain.w; // 该顶点沿河流中轴线的距离（米）

    // 横向归一化坐标，[-1, 1]，用于 Front LUT 采样
    float lateral = clamp(
        useProgressField ? progressField.a : riverCoord.g, 
        -1.0, 1.0);// 横向归一化坐标 [-1, 1]

    // Front LUT 的横向坐标（映射到 [0,1]）
    float frontUClamped =
        clamp(
            lateral * 0.5 + 0.5,
            0.0,
            1.0
        );

    float offsetMeters = 0.0;

    float lateralSquared =
        lateral * lateral;

    // float curvatureOffset =
    //     river.bore.y *
    //     riverCoord.a *
    //     lateralSquared; // 曲率修正项 river.bore.y 是曲率影响范围（米），riverCoord.a 是该点的曲率权重。
    // // lateral * lateral 使得河道外侧（|lateral| 大）波前被延迟（curvatureOffset 为正），内侧被提前，
    // // 从而在弯道处产生拉伸/压缩效果，避免波前断裂
    float curvatureOffset = 0.0;

    // 波前位置：该顶点沿河进度 - 涌潮波前已推进的距离
    float signedDistance =
        progressMeters -
        river.bore.x -
        curvatureOffset;

    vec2 localFrontNormal =
        localFlowDirection;

    // 水域掩码：岸外平滑衰减
    float waterMask = smoothstep(
        0.05, 0.95, 
        useProgressField ? progressField.g : riverFlow.a
        );

    // 涌潮振幅倍率（钳位安全值）
    // float boreAmplitude =
    //     clamp(
    //         riverFlow.b,
    //         0.0,
    //         2.0
    //     );
    float boreAmplitude = useProgressField ? progressField.b : riverFlow.b;

    // 波前长度掩码：在弯曲河道中直接用水域掩码替代直线波前的 lengthMask
    float lengthMask =
        waterMask;

    // 振幅与泡沫系数均受河流场控制
    float amplitudeMultiplier =
        boreAmplitude;

    float foamMultiplier =
        boreAmplitude *
        waterMask;

    float profilePhaseOffset = 0.0;

    float profileHalfWidth =
        profileConfig.domain.x;

    float profileU =
        clamp(
            signedDistance /
            (2.0 * profileHalfWidth) +
            0.5,
            0.0,
            1.0
        );

    float profileDuration =
        max(profileConfig.domain.w, 0.001);

    float profileTime =
        profileConfig.animation.x;

    float profileV =
        clamp(
            profileTime / profileDuration,
            0.0,
            1.0
        );

    vec2 profileUV =
        vec2(profileU, profileV);

    vec4 boreProfile =
        textureLod(
            boreProfileDisplacement,
            profileUV,
            0.0
        );

    vec4 boreDerivative =
        textureLod(
            boreProfileDerivative,
            profileUV,
            0.0
        );

    // ===== 第一步：分别采样三个 Cascade 层，获取独立的位移、斜率和泡沫判据 =====
    // 注意：这里用新的 SampleCascade 返回结构体，各层数据完全独立，
    // 后续可以自由地对某一层进行抑制或与其他效果（如 Bore）混合。

    // 采样第0层（短波 / 高频）：产生最细碎的毛细波，用于近处高细节水面
    CascadeSample shortWave =
        SampleCascade(
            fftDisplacement0,           // 第0层位移纹理（短波）
            fftNormalAux0,              // 第0层法线辅助纹理
            baseWorldPosition.xz,       // 世界空间水平坐标
            water.patchLengths.x,       // 第0层补丁边长（最小，如 16m）
            water.amplitudeScales.x     // 第0层振幅缩放
        );

    // 采样第1层（中频 / 中波）：产生中等尺度的波浪，用于中距离水面
    CascadeSample midWave =
        SampleCascade(
            fftDisplacement1,           // 第1层位移纹理（中波）
            fftNormalAux1,              // 第1层法线辅助纹理
            baseWorldPosition.xz,
            water.patchLengths.y,       // 第1层补丁边长（中等，如 64m）
            water.amplitudeScales.y     // 第1层振幅缩放
        );

    // 采样第2层（长波 / 低频）：产生最宏观的涌浪，用于远处水面和大尺度波浪
    CascadeSample longWave =
        SampleCascade(
            fftDisplacement2,           // 第2层位移纹理（长波）
            fftNormalAux2,              // 第2层法线辅助纹理
            baseWorldPosition.xz,
            water.patchLengths.z,       // 第2层补丁边长（最大，如 256m）
            water.amplitudeScales.z     // 第2层振幅缩放
        );

    // ===== 第二步：从 UBO 中读取涌潮波前的控制参数 =====

    // 涌潮整体开关（1.0 = 启用，0.0 = 关闭）
    float boreEnabled =
        bore.motionDebug.w;

    // 涌潮剖面动画是否激活（1.0 = 启用 Wave Profile 纹理采样）
    float profileEnabled =
        profileConfig.animation.w;

    // 当前片段是否在涌潮活跃区域内（如河道 SDF 或 Flow Map 控制）
    float activeRegionMask =
        profileConfig.geometry.w;

    // 全局振幅缩放（控制涌潮整体高度）
    float globalAmplitude =
        profileConfig.geometry.x;

    // 前向水平位移缩放（控制涌潮推挤强度）
    float forwardScale =
        profileConfig.geometry.y;

    // 向上垂直位移缩放（控制涌潮波高）
    float upwardScale =
        profileConfig.geometry.z;

    // ===== 第三步：计算涌潮强度与浪尖掩码 =====
    // float crestStrength =
    //     boreEnabled *               // 用户是否开启涌潮效果（键盘 B 键）
    //     profileEnabled *            // Wave Profile 是否启用
    //     activeRegionMask *          // 当前点是否在涌潮区域内
    //     lengthMask *                // 波前两端淡入淡出掩码
    //     amplitudeMultiplier *       // Front LUT 的振幅乘数（G 通道）
    //     globalAmplitude;            // 全局振幅缩放

    float commonBoreMask =
        boreEnabled *
        profileEnabled *
        activeRegionMask *
        waterMask;

    float boreStrength =
        commonBoreMask *
        boreAmplitude *
        globalAmplitude;

    float riseStrength =
        commonBoreMask *
        boreAmplitude;

    // 浪尖掩码：标记当前点是否位于涌潮波峰附近
    // 用于后续抑制背景 FFT 波浪，避免背景波浪破坏潮头形状
    float crestMask =
        boreProfile.a *
        commonBoreMask;

    // ===== 第四步：对每个 Cascade 层施加 FFT 抑制 =====

    // 从 UBO 中读取三层抑制系数（x=短波抑制，y=中波抑制，z=长波抑制）
    // 典型值：短波 0.2~0.4，中波 0.4~0.7，长波 0.7~1.0
    // 短波在潮头处被压得最狠，长涌浪保留最多，这样更自然
    vec3 suppressionAtCrest =
        profileConfig.suppression.xyz;

    // 短波权重：潮头处用 suppressionAtCrest.x，其余位置保持 1.0
    float shortWeight =
        mix(
            1.0,                        // 无涌潮区域：FFT 正常
            suppressionAtCrest.x,       // 潮头浪尖区域：抑制 FFT 短波
            crestMask                   // 混合因子：浪尖越强，抑制越重
        );

    // 中波权重（同理）
    float midWeight =
        mix(1.0, suppressionAtCrest.y, crestMask);

    // 长波权重（同理，通常保留较多，因为长涌浪不易被涌潮破坏）
    float longWeight =
        mix(1.0, suppressionAtCrest.z, crestMask);

    // FFT 整体开关
    float fftEnabled =
        water.metadata.y != 0 ? 1.0 : 0.0;

    // ===== 第五步：合成最终的 FFT 位移与斜率 =====

    // 将三个 Cascade 层分别乘以各自抑制权重后累加，再乘以 FFT 总开关
    vec3 fftDisplacement =
        (
            shortWave.displacement * shortWeight +   // 短波贡献（潮头处被抑制）
            midWave.displacement   * midWeight   +   // 中波贡献
            longWave.displacement  * longWeight      // 长波贡献（保留最多）
        ) * fftEnabled;

    // FFT 斜率同理
    vec2 fftSlope =
        (
            shortWave.slope * shortWeight +
            midWave.slope   * midWeight   +
            longWave.slope  * longWeight
        ) * fftEnabled;

    // ===== 第六步：保留泡沫判据（从三层中取最危险的那个） =====

    // breakingHint 取三个 Cascade 层中的最大值
    // Jacobian 越小（越负），破碎越可能，越应出现白浪
    float breakingHint =
        max(
            max(shortWave.breaking, midWave.breaking),
            longWave.breaking
        ) * fftEnabled;

    // ===== 第七步：打包位移向量（xyz = 位移，w = 泡沫强度） =====

    // 将 FFT 位移和泡沫判据打包成 vec4
    // 后续会在此基础之上叠加 Bore 位移
    vec4 displacement =
        vec4(
            fftDisplacement,           // xyz：FFT 水平位移 + 高度
            1.0 - breakingHint         // w：泡沫强度（1 = 无泡沫，0 = 泡沫最强）
        );


    // 计算用于可视化或调试的 FFT UV（这里取中层补丁的 UV）
    vec2 fftUV =
        fract(baseWorldPosition.xz / water.patchLengths.y);


    // ===== 第一步：计算涌潮的水平位移 =====
    // 水平位移沿局部波前法线方向，强度由 Wave Profile 的 R 通道 (forward displacement)、缩放因子和涌潮总强度决定
    vec2 boreHorizontal =
        localFrontNormal *          // 局部波前法线方向（与波前垂直，指向推进方向）
        boreProfile.r *             // Wave Profile 位移纹理的 R 通道（前向水平位移）
        forwardScale *              // 从 UBO 读取的前向位移缩放因子
        boreStrength;               // 涌潮总强度（由多个开关和掩码连乘得出）

    // ===== 第二步：计算涌潮的垂直位移（直接波高 + 潮后水位抬升） =====

    // 直接波高：由 Wave Profile 的 G 通道 (upward displacement) 控制
    float boreLocalVertical =
        boreProfile.g *             // Wave Profile 位移纹理的 G 通道（向上垂直位移 / 波高）
        upwardScale *               // 从 UBO 读取的向上位移缩放因子
        boreStrength;               // 涌潮总强度

    // 潮后水位抬升宽度（米），避免除零
    float riseWidth =
        max(profileConfig.domain.z, 0.001);

    // 潮后掩码：波前后方（signedDistance < 0）为 1，前方为 0，过渡区平滑
    float backMask =
        1.0 -
        smoothstep(
            -riseWidth,             // 开始过渡的位置（波前后方）
            riseWidth,              // 结束过渡的位置（波前前方）
            signedDistance          // 当前顶点到波前线的带符号距离
        );

    // 潮后整体水位抬升：涌潮经过后，水面整体上升一定高度，模拟涨潮
    float waterRise =
        profileConfig.domain.y *    // 从 UBO 读取的水位抬升高度
        backMask *                  // 只在波前后方生效
        riseStrength;

    // 最终垂直位移 = 直接波高 + 潮后水位抬升
    float boreVertical =
        boreLocalVertical +
        waterRise;

    // ===== 第三步：计算涌潮的局部坡度（用于法线重建） =====

    // 垂直位移对距离的导数（Wave Profile 导数纹理的 G 通道）
    float dUpwardDs =
        boreDerivative.g *          // d(upward)/ds，即波高随距离的变化率
        upwardScale *               // 缩放
        boreStrength;

    // 潮后水位抬升掩码的导数（smoothstep 的解析导数）
    float dBackMaskDs =
        -SmoothStepDerivative(      // 负号：因为 backMask = 1 - smoothstep
            -riseWidth,             // 过渡区起点
            riseWidth,              // 过渡区终点
            signedDistance          // 当前距离
        );

    // 水位抬升的坡度
    float dWaterRiseDs =
        profileConfig.domain.y *
        dBackMaskDs *
        riseStrength;

    // 前向水平位移对距离的导数（Wave Profile 导数纹理的 R 通道）
    float dForwardDs =
        clamp(
            boreDerivative.r *          // d(forward)/ds
            forwardScale *              // 缩放
            boreStrength,
            -0.65,
            0.65
        );

    // ===== 第四步：参数化曲面修正（关键！） =====

    // 分母：1 + dForwardDs，表示水平位移导致的水面拉伸或压缩
    // 当波峰向前推挤时（dForwardDs > 0），水面被拉伸，坡度被“稀释”
    // 当波峰后方向后拉时（dForwardDs < 0），水面被压缩，坡度被“浓缩”
    // 限制最小值 0.2，避免分母过小导致坡度爆炸
    float denominator =
        max(
            1.0 + dForwardDs,       // 参数化曲面修正项
            0.2                     // 最小裁剪值，防止奇异性
        );

    // 有效涌潮坡度：将垂直位移的总导数（波高 + 水位抬升）除以修正分母
    float effectiveBoreSlope =
        clamp(
            (dUpwardDs + dWaterRiseDs) / denominator,
            -2.5,
            2.5
        );

    // 坡度沿局部波前法线方向
    vec2 boreSlope =
        effectiveBoreSlope *
        localFrontNormal;

    vec2 multiFoamVelocity = vec2(0.0);
    float multiFoamSourceWeight = 0.0;
    float multiProfileFoam = 0.0;
    float multiBreakingFoam = 0.0;

    if(multiBore.metadata.z != 0 && multiBore.metadata.x > 0){
        boreHorizontal = vec2(0.0);
        boreVertical = 0.0;
        boreSlope = vec2(0.0);
        crestMask = 0.0;
        shortWeight = 1.0;
        midWeight = 1.0;
        longWeight = 1.0;

        float totalCrestWeight = 0.0;
        float waterRiseMask = 0.0;
        float activeCount = float(min(multiBore.metadata.x, multiBore.metadata.y));

        for(int eventIndex = 0; eventIndex < min(multiBore.metadata.x, multiBore.metadata.y); ++eventIndex){
            BoreEventGPU event = boreEvents.events[eventIndex];

            if(event.motion.w < 0.5){
                continue;
            }

            float eventProgress = event.motion.x;
            float eventWidthScale = max(event.shape.y, 0.05);
            float eventHalfWidth = profileHalfWidth * eventWidthScale;
            float eventCurvatureOffset = river.bore.y * event.shape.w * riverCoord.a * lateralSquared;
            float eventSignedDistance = progressMeters - eventProgress - eventCurvatureOffset;
            float eventProfileU = clamp(eventSignedDistance / (2.0 * eventHalfWidth) + 0.5, 0.0, 1.0);
            float eventProfileV = clamp(event.appearance.y, 0.0, 1.0);

            vec4 eventProfile = textureLod(boreProfileDisplacement, vec2(eventProfileU, eventProfileV), 0.0);
            vec4 eventDerivative = textureLod(boreProfileDerivative, vec2(eventProfileU, eventProfileV), 0.0);

            float eventAmplitude = boreAmplitude * event.shape.x;

            // 未被浪脊噪声调制的基础振幅：潮后水位抬升必须用它。
            // crestField 是高频且随 eventProgress 快速滚动的 FBM，而 eventBackMask
            // 在潮后恒为 1，一旦让它进入水位就会把高频噪声铺满整片过潮水面，
            // 表现为高速移动的细波浪线。噪声只应作用于浪脊本身。
            float eventAmplitudeBase = eventAmplitude;

            // ===== 浪脊坐标系噪声：打破"一堵墙" =====
            // x 轴：横向 lateral 铺开(控制沿河宽度方向的团块数) + 顶点沿河位置 progressMeters 提供二维细节
            // y 轴：eventProgress 让整块噪声随潮头推进而流动(动画，不再冻在世界里)
            // event.appearance.z：每个潮头独立种子，彼此不同
            vec2 crestUV =
                vec2(lateral * multiBore.crestNoiseA.x + progressMeters * multiBore.crestNoiseA.y,
                     eventProgress * multiBore.crestNoiseA.w + progressMeters * multiBore.crestNoiseA.z)
                + event.appearance.z;

            crestUV = fract(crestUV * 0.01) * 100.0;

            float crestBig    = FBM2(crestUV);                              // 大尺度起伏
            float crestDetail = FBM2(crestUV * multiBore.crestNoiseB.x);    // 细小复杂波动
            float crestField  = mix(crestBig, crestDetail, multiBore.crestNoiseB.y);

            // 幅度调制：[振幅下限, 振幅上限]，高低差
            float amplitudeVariation = mix(multiBore.crestNoiseB.z, multiBore.crestNoiseB.w, crestField);
            eventAmplitude *= amplitudeVariation;

            float eventCommonMask = commonBoreMask;
            float eventStrength = eventCommonMask * eventAmplitude * globalAmplitude;
            float eventRiseStrength = eventCommonMask * eventAmplitudeBase;
            float eventCrestMask = eventProfile.a * eventCommonMask;

            vec2 eventHorizontal =
                localFrontNormal *
                eventProfile.r *
                forwardScale *
                event.shape.z *
                eventStrength;

            float eventLocalVertical =
                eventProfile.g *
                upwardScale *
                eventStrength;

            // 浪脊顶边参差：高频噪声[-1,1]只叠在波峰(eventProfile.a)附近，
            // 让"墙顶"变成起伏的浪脊，只改高度不动 signedDistance，避免撕裂
            // 顶抖抗走样：粗 LOD tile（大三角）顶点间距大，高频噪声必然走样 → 直接淡出
            float wobbleLodFade = 1.0 - smoothstep(64.0, 256.0, tile.originSize.z);
            // 单层 ValueNoise：最高频就等于 crestNoiseC.y，不会像 FBM 那样再翻 16 倍
            float crestTopWobble = (ValueNoise(crestUV * multiBore.crestNoiseC.y) - 0.5) * 2.0;
            eventLocalVertical +=
                crestTopWobble *
                eventProfile.a *
                upwardScale *
                eventStrength *
                multiBore.crestNoiseC.x *
                wobbleLodFade;

            float eventBackMask =
                1.0 - smoothstep(-riseWidth, riseWidth, eventSignedDistance);

            waterRiseMask = max(waterRiseMask, eventBackMask * eventRiseStrength);

            float eventDUpwardDs =
                eventDerivative.g *
                upwardScale *
                eventStrength;

            float eventDBackMaskDs =
                -SmoothStepDerivative(-riseWidth, riseWidth, eventSignedDistance);

            float eventDWaterRiseDs =
                profileConfig.domain.y *
                eventDBackMaskDs *
                eventRiseStrength;

            float eventDForwardDs =
                clamp(
                    eventDerivative.r *
                    forwardScale *
                    event.shape.z *
                    eventStrength,
                    -0.65,
                    0.65
                );

            float eventDenominator = max(1.0 + eventDForwardDs, 0.2);
            float eventEffectiveSlope =
                clamp((eventDUpwardDs + eventDWaterRiseDs) / eventDenominator, -2.5, 2.5);

            boreHorizontal += eventHorizontal;
            boreVertical += eventLocalVertical;
            boreSlope += eventEffectiveSlope * localFrontNormal;
            crestMask = max(crestMask, eventCrestMask);
            totalCrestWeight += eventCrestMask;

            shortWeight = min(shortWeight, mix(1.0, event.suppression.x, eventCrestMask));
            midWeight = min(midWeight, mix(1.0, event.suppression.y, eventCrestMask));
            longWeight = min(longWeight, mix(1.0, event.suppression.z, eventCrestMask));

            // 离波前窗口：eventProfileU 在 |eventSignedDistance|>eventHalfWidth 时被 clamp 到 0/1，
            // 会继承剖面纹理边缘的非零 rearFoam，导致潮头已过的整片下游铺满常量泡沫(网格/暗块 bug)。
            // 超出剖面域即把泡沫淡到 0，只保留波前附近的真实泡沫。
            float eventFrontFoamWindow =
                1.0 - smoothstep(eventHalfWidth * 0.85, eventHalfWidth, abs(eventSignedDistance));

            float eventProfileFoam = eventProfile.b * eventAmplitude * eventCommonMask * event.appearance.x
                                      * eventFrontFoamWindow;
            float eventBreakingFoam = eventDerivative.a * eventAmplitude * eventCommonMask * event.appearance.x
                                      * mix(0.6, 1.4, crestField)
                                      * eventFrontFoamWindow;
            
            multiProfileFoam = 1.0 - (1.0 - multiProfileFoam) * (1.0 - eventProfileFoam);
            multiBreakingFoam = 1.0 - (1.0 - multiBreakingFoam) * (1.0 - eventBreakingFoam);

            float eventFoamSource = max(eventProfileFoam, eventBreakingFoam);
            multiFoamVelocity += localFrontNormal * eventDerivative.b * eventFoamSource;
            multiFoamSourceWeight += eventFoamSource;
        }

        float overlap = max(totalCrestWeight, 1.0);
        boreHorizontal /= overlap;
        boreVertical = boreVertical / overlap + profileConfig.domain.y * waterRiseMask;
        boreSlope /= overlap;

        if(multiFoamSourceWeight > 1.0e-4){
            multiFoamVelocity /= multiFoamSourceWeight;
        }
    }

    // ===== 修复：让 Multi-bore 的 FFT 抑制真正作用到几何 =====
    // 上面第五步(613-626)在 multi-bore 循环之前就合成了 fftDisplacement/fftSlope，
    // 而循环会把 shortWeight/midWeight/longWeight 重置为 1 再按各事件 min()，
    // 所以必须在循环结束后重新合成一次。否则多潮头的 suppression 只体现在
    // debug 输出里，潮头浪面依然铺满未被抑制的 FFT 短波。
    // 单潮头路径下权重未被改动，此处重算结果与 613-626 完全相同，故无需分支。
    fftDisplacement =
        (
            shortWave.displacement * shortWeight +
            midWave.displacement   * midWeight   +
            longWave.displacement  * longWeight
        ) * fftEnabled;

    fftSlope =
        (
            shortWave.slope * shortWeight +
            midWave.slope   * midWeight   +
            longWave.slope  * longWeight
        ) * fftEnabled;

    // 打包向量同步更新，否则 fragDisplacement（调试模式 1/2/4 及片元里的 heightFactor）
    // 与真实几何不一致
    displacement.xyz = fftDisplacement;

    // ===== 第五步：合成最终世界坐标（FFT + Bore） =====
    // 水平位移：FFT 水平位移（缩放后）+ 涌潮水平位移
    // 垂直位移：FFT 高度 + 涌潮垂直位移
    vec3 finalDisplacement =
        vec3(
            fftDisplacement.x *         // FFT 水平位移（已包含三个 Cascade 的加权结果）
                water.simulation.y *    // choppy 强度
                5.0 +                   // 视觉效果放大因子
                boreHorizontal.x,       // 涌潮水平位移
            fftDisplacement.y *         // FFT 垂直位移（高度）
                10.0 +                  // 视觉效果放大因子
                boreVertical,           // 涌潮垂直位移（波高 + 水位抬升）
            fftDisplacement.z *
                water.simulation.y *
                5.0 +
                boreHorizontal.y
        );

    vec3 worldPosition =
        baseWorldPosition +
        finalDisplacement;
    
    // 开启 Skirt 裙边向下拉，遮住 LOD 接缝
    // if(false && inSkirt > 0.5){
    if(false && inSkirt > 0.5){
        worldPosition.y -= 15.0;
    }

    // ===== 第六步：合成最终坡度并重建法线 =====

    // 总坡度 = FFT 坡度 + 涌潮坡度
    vec2 totalSlope =
        fftSlope *                  // FFT 斜率（三个 Cascade 加权后）
        water.simulation.z +        // 法线扰动强度
        boreSlope;                  // 涌潮坡度

    // 单潮头回退路径的离波前窗口：与多潮头一致，超出剖面域时把泡沫淡到 0，
    // 避免继承剖面边缘 rearFoam 造成下游整片铺泡沫。
    float singleFrontFoamWindow =
        1.0 - smoothstep(profileHalfWidth * 0.85, profileHalfWidth, abs(signedDistance));

    float profileFoam =
        multiBore.metadata.z != 0 && multiBore.metadata.x > 0
        ? multiProfileFoam
        : boreProfile.b *
            boreAmplitude *
            commonBoreMask *
            singleFrontFoamWindow;

    float fftJacobianFoam =
        smoothstep(
            foam.thresholds.z,
            foam.thresholds.w,
            breakingHint
        );

    float slopeMagnitude =
        length(totalSlope);

    float slopeFoam =
        smoothstep(
            foam.thresholds.x,
            foam.thresholds.y,
            slopeMagnitude
        );

    float boreBreakingFoam =
        multiBore.metadata.z != 0 && multiBore.metadata.x > 0
        ? multiBreakingFoam
        : boreDerivative.a *
            boreAmplitude *
            commonBoreMask *
            singleFrontFoamWindow;

    vec2 boreFlowVelocity =
        multiBore.metadata.z != 0 && multiBore.metadata.x > 0
        ? multiFoamVelocity
        : localFrontNormal *
            boreDerivative.b;
    
    // 从坡度重建世界空间法线：原法线为 (0, 1, 0)，经坡度扰动后归一化
    vec3 worldNormal =
        normalize(
            vec3(
                -totalSlope.x,      // X 方向的负斜率
                1.0,                // Y 分量保持为 1（法线朝上为主）
                -totalSlope.y       // Z 方向的负斜率
            )
        );

    // 将世界空间的位置、法线、UV 以及调试数据传递给片段着色器
    fragWorldPosition = worldPosition;
    fragWorldNormal = worldNormal;
    fragUV = fftUV;
    fragDisplacement = displacement;
    fragNormalAux = vec4(totalSlope, 0.0, 0.0);

    fragBoreDebug0 = vec4(
        signedDistance,
        lengthMask,
        frontUClamped,
        amplitudeMultiplier
    );

    fragBoreDebug1 = vec4(
        localFrontNormal,
        foamMultiplier,
        profilePhaseOffset
    );

    fragBoreProfile0 =
        vec4(
            boreProfile.r,
            boreProfile.g,
            boreProfile.b,
            boreProfile.a
        );

    fragBoreProfile1 =
        vec4(
            boreDerivative.r,
            boreDerivative.g,
            boreDerivative.b,
            boreDerivative.a
        );

    fragComposition =
        vec4(
            shortWeight,
            midWeight,
            longWeight,
            backMask
        );

    fragFinalDisplacement =
        vec4(
            finalDisplacement,
            0.0
        );

    fragSlopeDebug =
        vec4(
            boreSlope,
            totalSlope
        );

    fragFoamSourceData =
        vec4(
            profileFoam,
            fftJacobianFoam,
            slopeFoam,
            boreBreakingFoam
        );

    fragFoamFlowData =
        vec4(
            boreFlowVelocity,
            lengthMask *
                boreEnabled *
                profileEnabled *
                activeRegionMask,
            crestMask
        );

    // MVP 变换到裁剪空间，输出最终顶点位置
    gl_Position =
        camera.projection *
        camera.view *
        vec4(worldPosition, 1.0);
}