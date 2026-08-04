#version 450

#extension GL_GOOGLE_include_directive : require

#include "common/foam_common.glsl"
#include "common/water_optics_common.glsl"

// set 和 binding 是用于绑定 Uniform Buffer、纹理、存储缓冲 这类全局资源的“外部接口”。
// location 是用于连接顶点数据缓冲区和着色器间数据传递的“内部管道”。

// 从顶点着色器传入的世界空间位置
layout(location = 0) in vec3 fragWorldPosition;
// 世界空间法线
layout(location = 1) in vec3 fragWorldNormal;
// 用于位移纹理采样的 UV 坐标
layout(location = 2) in vec2 fragUV;
// 位移纹理采样结果：x=dispX, y=height, z=dispZ, w=jacobian
layout(location = 3) in vec4 fragDisplacement;
// 法线辅助纹理采样结果：x=slopeX, y=slopeZ, z=dDxdx, w=dDzdz
layout(location = 4) in vec4 fragNormalAux;

layout(location = 5) in vec4 fragBoreDebug0;
layout(location = 6) in vec4 fragBoreDebug1;

layout(location = 7) in vec4 fragBoreProfile0;
layout(location = 8) in vec4 fragBoreProfile1;
layout(location = 9) in vec4 fragComposition;
layout(location = 10) in vec4 fragSlopeDebug;
layout(location = 11) in vec4 fragFoamSourceData;
layout(location = 12) in vec4 fragFoamFlowData;
layout(location = 13) in vec4 fragFinalDisplacement;
layout(location = 14) in vec4 fragRiverFlow;
layout(location = 15) in vec4 fragRiverCoord;
layout(location = 16) in vec4 fragShore;

// 摄像机 UBO（绑定 set=0, binding=0）
layout(set = 0, binding = 0) uniform CameraUBO {
    mat4 model;         // 物体自身变换（平移、旋转、缩放）
    mat4 view;          // 相机视图变换
    mat4 projection;    // 透视投影
    vec4 cameraWorldPosition; // 摄像机位置
    ivec4 debug;        // 调试模式标志（x=模式编号）
} camera;

// 水体参数 UBO（绑定 set=0, binding=1）
layout(set = 0, binding = 1) uniform WaterParamsUBO {
    vec4 patchLengths;      // 补丁边长（x分量有效）
    vec4 amplitudeScales;   // 振幅缩放（x分量有效）
    ivec4 metadata;         // 元数据（如Cascade层数）
    vec4 simulation;        // 模拟参数（x=时间，y=choppy强度，z=法线扰动强度，w=调试模式）
} water;

// Foam参数 UBO（绑定 set=1, binding=0）
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

layout(set = 1, binding = 1) uniform sampler2D foamDetailTexture;

layout(set = 1, binding = 2) uniform sampler2D foamState0;
layout(set = 1, binding = 3) uniform sampler2D foamState1;

layout(set = 1, binding = 4) uniform WaterMaterialUBO
{
    vec4 shallowColor;
    vec4 deepColor;
    vec4 sedimentColor;
    vec4 opticalParams;
    vec4 lightParams;
    vec4 fogParams;
    vec4 absorptionCoeff;
    vec4 shallowParams;
    vec4 absorptionShore;
    vec4 scatteringDeep;
    vec4 scatteringShore;
    vec4 shoreBlend;
    vec4 colorBehind;

    vec4 waterSpecular;
    vec4 specularHorizon;
    vec4 cheapScatter;
} material;

// 片段着色器输出颜色
layout(location = 0) out vec4 outColor;

// 将二维向量转换为带色的可视化颜色：红色通道对应x，蓝色通道对应z，绿色固定0.5
vec3 SignedColor(vec2 value, float scale)
{
    return vec3(
        0.5 + value.x * scale,
        0.5,
        0.5 + value.y * scale
    );
}

float SoftUnion(float a, float b)
{
    return 1.0 - (1.0 - a) * (1.0 - b);
}

void main()
{
    int mode = camera.debug.x;
    // 矩形 Quadtree 视觉裁成 U 形河道
    if(fragRiverFlow.a < 0.25){
        discard;
    }

    float profileFoam =
        fragFoamSourceData.r;

    float fftJacobianFoam =
        fragFoamSourceData.g;

    float slopeFoam =
        fragFoamSourceData.b;

    float boreBreakingFoam =
        fragFoamSourceData.a;

    float localBoreFoam =
        max(
            profileFoam *
                foam.sourceStrength.x,
            boreBreakingFoam *
                foam.sourceStrength.w
        );

    float globalOceanFoam =
        max(
            slopeFoam *
                foam.sourceStrength.y,
            fftJacobianFoam *
                foam.sourceStrength.z
        );

    // FFT 全局泡沫随 patch 平铺出现规则网格，远处缩小后严重走样(越远越密)。
    // 按相机距离淡出：近处保留，远处走样网格抹掉。近/远阈值由 GUI 提供(foam.runtime.z/.w)。
    float oceanFoamCameraDist =
        length(fragWorldPosition - camera.cameraWorldPosition.xyz);
    float oceanFoamDistanceFade =
        1.0 - smoothstep(foam.runtime.z, foam.runtime.w, oceanFoamCameraDist);
    globalOceanFoam *= oceanFoamDistanceFade;

    float foamSource =
        max(
            localBoreFoam,
            globalOceanFoam
        );

    vec2 foamVelocity =
        fragFoamFlowData.xy;

    float cycleDuration =
        max(foam.animation.y, 0.001);

    float basePhase =
        foam.animation.x /
        cycleDuration;

    float phase0 =
        fract(basePhase + 0.0);

    float phase1 =
        fract(basePhase + 1.0 / 3.0);

    float phase2 =
        fract(basePhase + 2.0 / 3.0);

    float w0 =
        FoamPhaseWeight(phase0);

    float w1 =
        FoamPhaseWeight(phase1);

    float w2 =
        FoamPhaseWeight(phase2);

    float weightSum =
        max(
            w0 + w1 + w2,
            1.0e-5
        );

    w0 /= weightSum;
    w1 /= weightSum;
    w2 /= weightSum;

    FoamDetail detail0 =
        SampleFoamPhase(
            foamDetailTexture,
            fragWorldPosition.xz,
            foamVelocity,
            phase0,
            cycleDuration,
            foam.animation.z
        );

    FoamDetail detail1 =
        SampleFoamPhase(
            foamDetailTexture,
            fragWorldPosition.xz,
            foamVelocity,
            phase1,
            cycleDuration,
            foam.animation.z
        );

    FoamDetail detail2 =
        SampleFoamPhase(
            foamDetailTexture,
            fragWorldPosition.xz,
            foamVelocity,
            phase2,
            cycleDuration,
            foam.animation.z
        );

    float detailCoverage =
        detail0.coverage * w0 +
        detail1.coverage * w1 +
        detail2.coverage * w2;

    vec2 detailNormalSum =
        detail0.normalXZ * w0 +
        detail1.normalXZ * w1 +
        detail2.normalXZ * w2;

    // 三相细节法线加权后可能相互抵消到接近零向量(尤其泡沫初期)，
    // 直接 normalize 会除零产生 NaN，传播到法线/光照 → 渲染成黑色；
    // 相位滚动使零点移动 → 表现为"移动的黑色条纹"。零长度时回退为无扰动。
    vec2 detailNormal =
        length(detailNormalSum) > 1.0e-5
        ? normalize(detailNormalSum)
        : vec2(0.0);

    float breakup =
        detail0.breakup * w0 +
        detail1.breakup * w1 +
        detail2.breakup * w2;

    float patternedSource =
        foamSource *
        mix(
            0.55,
            1.45,
            detailCoverage
        );

    patternedSource *=
        mix(
            0.7,
            1.0,
            breakup
        );

    float foamCoverage =
        smoothstep(
            foam.appearance.x,
            foam.appearance.x +
                foam.appearance.y,
            patternedSource
        );

    vec2 stateUV =
        (
            fragWorldPosition.xz -
            foam.domain.xy
        ) /
        foam.domain.zw;

    float stateFoam =
        foam.runtime.x < 0.5
        ? texture(foamState0, stateUV).r
        : texture(foamState1, stateUV).r;

    float finalFoam =
        SoftUnion(
            foamCoverage,
            stateFoam * foam.appearance.w
        );

    // 模式 0：完整水体着色（光照 + 反射 + 高光 + 泡沫 + 雾）
    if(mode == 0){
        // ===== 1. 法线准备与泡沫微观扰动 =====
        // 基础几何法线（由波浪形状决定）
        vec3 baseNormal = normalize(fragWorldNormal);

        // 构建切线空间：根据 baseNormal 计算两个正交的切线方向
        // 如果法线接近垂直向上（abs(y) < 0.99），用 world up 做叉积得到切线；否则用 world right
        vec3 tangent = normalize(
            abs(baseNormal.y) < 0.99
            ? cross(vec3(0.0, 1.0, 0.0), baseNormal)
            : vec3(1.0, 0.0, 0.0)
        );
        vec3 bitangent = normalize(cross(baseNormal, tangent));

        // 利用泡沫细节纹理的法线信息，对基础法线进行微观扰动
        // detailNormal.xy 是泡沫细节纹理中的法线扰动向量（范围为 [-1, 1]）
        // foam.appearance.z 是法线扰动强度，控制凹凸的明显程度
        vec3 foamPerturbedNormal = normalize(
            baseNormal +
            tangent * detailNormal.x * foam.appearance.z +
            bitangent * detailNormal.y * foam.appearance.z
        );

        // 根据泡沫覆盖率 finalFoam 在平滑水面法线和粗糙泡沫法线之间插值
        // 泡沫越浓，法线越粗糙（越偏离平滑方向）
        vec3 normal = normalize(mix(baseNormal, foamPerturbedNormal, finalFoam));

        // ===== 2. 光照向量初始化 =====
        // 视线方向 V：从片段指向摄像机（世界空间）
        vec3 V = normalize(camera.cameraWorldPosition.xyz - fragWorldPosition);

        // 光源方向 L：太阳光方向（从表面指向光源，如果 lightParams 是从太阳射出的方向需取反）
        vec3 L = normalize(material.lightParams.xyz);

        // FF MF_ImposibleNormalFix：修正掠射角下"反射进水面以下"的不可能法线，
        // 消除远处水面的黑条纹与错误明暗
        normal = ImpossibleNormalFix(normal, V, material.cheapScatter.w);

        // 半角向量 H：视线和光源的中间方向，用于 Blinn-Phong 高光
        vec3 H = normalize(L + V);

        // 反射向量 R：视线经法线反射后的方向，用于天空反射采样
        // reflect 的第一个参数是入射方向，即从表面指向摄像机的向量 -V
        vec3 R = reflect(-V, normal);

        // NdotV：视线与法线的夹角余弦，用于菲涅尔反射
        float NdotV = clamp(dot(normal, V), 0.0, 1.0);

        // NdotL：光源与法线的夹角余弦，用于漫反射
        float NdotL = clamp(dot(normal, L), 0.0, 1.0);

        // ===== 3. FF MF_CoastlineColor：岸线因子 =====
        // Shoreline = 1 - max(深度/17m, 离岸距离/200m)：又浅又靠岸 → 1
        float bedHeight  = fragShore.a;
        float waterDepth = max(fragWorldPosition.y - bedHeight, 0.0);
        float bankDist   = max(fragShore.r, 0.0);

        float scatterHeight   = clamp(waterDepth / max(material.shoreBlend.x, 0.001), 0.0, 1.0);
        float scatterDistance = clamp(bankDist   / max(material.shoreBlend.y, 0.001), 0.0, 1.0);
        float shoreline = 1.0 - max(scatterHeight, scatterDistance);

        // ===== FF MF_WaterTransition：深水档 ↔ 岸线档（Painter=0，两档 lerp）=====
        vec3 absorption = mix(material.absorptionCoeff.rgb, material.absorptionShore.rgb, shoreline);
        // FF 对散射用 Pow2(Shoreline)，让浑浊只集中在最贴岸处
        vec3 scattering = mix(material.scatteringDeep.rgb, material.scatteringShore.rgb,
                              shoreline * shoreline);

        // ===== FF MF_SingleLayerWater：湍流削弱吸收、增强散射 =====
        // FF: WaveScattering 来自 MF_FluidScattering（Cheap 版），泡沫只是叠加项
        float cheapScatter = FluxCheapScattering(
            baseNormal, normal, V,
            material.cheapScatter.x,
            material.cheapScatter.y,
            material.cheapScatter.z);
        float waveScattering = max(finalFoam, cheapScatter);
        absorption /= (waveScattering + 1.0);
        scattering  = scattering * (waveScattering + 1.0)
                    + scattering * material.scatteringShore.w * finalFoam;

        // 吸收总倍率（GUI 单一"变不透明快慢"旋钮）
        absorption *= max(material.shallowParams.z, 0.0);

        // ===== Beer-Lambert：光程含 FF 的俯视加成 =====
        float pathLength = waterDepth * (abs(V.y) * material.shallowParams.w + 1.0);
        vec3  transmittance = exp(-pathLength * absorption);

        // 单次散射平衡色：深水收敛到 scattering/absorption，再乘入射光强
        vec3 sunLight = vec3(1.0, 0.96, 0.86) * material.scatteringDeep.w;
        vec3 mediumColor = scattering / max(absorption, vec3(1.0e-4))
                         * (1.0 - transmittance) * sunLight;

        // PhaseG 各向异性散射（Henyey-Greenstein）：朝太阳方向更亮
        float phaseG   = clamp(material.shoreBlend.z, -0.95, 0.95);
        float cosTheta = dot(-V, L);
        float g2 = phaseG * phaseG;
        float phase = (1.0 - g2) / pow(max(1.0 + g2 - 2.0 * phaseG * cosTheta, 1.0e-4), 1.5);
        mediumColor *= 0.5 + 0.5 * clamp(phase, 0.0, 4.0);

        vec3 waterColor = mediumColor;

        // ===== 4. FF Fresnel + 天空反射 =====
        // FF MF_Fresnel(bias, scale, power)：power=9 把反射集中到掠射角
        float fresnel = FluxFresnel(
            material.waterSpecular.x,
            material.waterSpecular.y,
            material.waterSpecular.z,
            normal, V);
        vec3 reflectedSky = SkyColor(R, L);

        // ===== 5. FF 高光：地平线衰减 + 双重门控 + FF 粗糙度模型 =====
        // ① 地平线衰减：超出"相机高度决定的可见距离"后只剩底噪，远景不再糊成白片
        float horizonFalloff = FluxSpecularHorizon(
            fragWorldPosition, camera.cameraWorldPosition.xyz,
            material.specularHorizon.x,
            material.specularHorizon.y,
            material.waterSpecular.w);
        horizonFalloff *= fresnel;

        // ② 透明度门控：极浅的水几乎不反射太阳（FF: saturate(Translucent)）
        float translucentGate =
            clamp(1.0 - dot(transmittance, vec3(0.2126, 0.7152, 0.0722)), 0.0, 1.0);

        // ③ min(..., normal.y)：竖直浪面不该有强高光，避免浪墙侧面爆白
        float specularMask = min(translucentGate * horizonFalloff,
                                 clamp(normal.y, 0.0, 1.0));

        vec3  Hs   = normalize(L + V);
        float NoH  = clamp(dot(normal, Hs), 0.0, 1.0);
        float NoLs = clamp(dot(normal, L), 0.0, 1.0);

        // ④ FF 粗糙度：掠射角变粗糙 → 远处高光变宽变暗
        float roughness = FluxWaterRoughness(
            NoH, NdotV,
            material.specularHorizon.z,
            material.specularHorizon.w);
        roughness = clamp(mix(roughness, 0.5, finalFoam), 0.02, 1.0);  // 泡沫区更粗糙

        float Dv  = D_GGX(NoH, roughness);
        float Vis = V_SmithGGX(NdotV, NoLs, roughness);
        vec3  sunColor = vec3(1.0, 0.96, 0.86);
        vec3  specular = sunColor *
            (Dv * Vis * NoLs * material.lightParams.w * specularMask);

        // ===== 6. 漫反射 + 菲涅尔合成 =====
        // 用 waterColor 替代原来的 bodyColor，不再混合河床反照率
        vec3 diffuse  = waterColor * (0.35 + 0.65 * NdotL);
        vec3 litColor = mix(diffuse, reflectedSky, fresnel);
        litColor += specular;

        // 泡沫覆盖
        litColor = mix(litColor, vec3(0.95, 0.97, 0.96), finalFoam);

        // ===== 7. 上岸渐隐已不再需要（真实地形通过 alpha 透出）=====

        // ===== 8. 距离雾融入天空（按视线方向取天空色，远处无缝接天）=====
        float distanceToCamera = length(camera.cameraWorldPosition.xyz - fragWorldPosition);
        vec3  viewDirWS = normalize(fragWorldPosition - camera.cameraWorldPosition.xyz);
        vec3  fogColor  = SkyColor(viewDirWS, L);
        litColor = ApplyDistanceFog(litColor, distanceToCamera,
            fogColor, material.fogParams.x, material.fogParams.y);

        // ===== 9. 深度驱动不透明度（MF_FluidWaterLayer 的 Translucent）=====
        // 不透明度 = 1 - 亮度加权透射率（与上面的 absorption 完全同源）
        float lumT = dot(transmittance, vec3(0.2126, 0.7152, 0.0722));
        float waterAlpha = clamp(1.0 - lumT, 0.0, 1.0);

        // 反射掉的能量不可能同时从水底透上来：不透明度至少等于反射比例。
        // 这让浅水在掠射角呈现镜面天空反射（图二的湿沙水膜），而不是半透明糊。
        waterAlpha = max(waterAlpha, fresnel);

        // 泡沫是不透明白沫，不能让河床透过来
        waterAlpha = max(waterAlpha, finalFoam);

        outColor = vec4(litColor, waterAlpha);
        return;
    }

    // 模式 1：高度场可视化（灰度图）
    if(mode == 1){
        // 将高度值映射到灰度：0.5为均值，高度越大越亮
        float h = fragDisplacement.y;
        outColor = vec4(vec3(0.5 + h * 1.0), 1.0);
        return;
    }

    // 模式 2：水平位移可视化（红蓝通道表示XZ位移）
    if(mode == 2){
        // x通道红，z通道蓝
        outColor = vec4(SignedColor(fragDisplacement.xz, 0.08), 1.0);
        return;
    }

    // 模式 3：法线扰动斜率可视化
    if(mode == 3){
        // slopeX红色，slopeZ蓝色
        outColor = vec4(SignedColor(fragNormalAux.xy, 0.25), 1.0);
        return;
    }

    // 模式 4：波浪破碎/泡沫判据可视化
    if(mode == 4){
        // jacobian越接近0或负值，breaking值越大（越白）
        float breaking = clamp(1.0 - fragDisplacement.a, 0.0, 1.0);
        // 白浪颜色：红黄调
        outColor = vec4(breaking, breaking * 0.4, 0.0, 1.0);
        return;
    }

    // 模式 5：世界空间法线可视化（RGB编码）
    if(mode == 5){
        // 法线从[-1,1]映射到[0,1]颜色
        outColor = vec4(normalize(fragWorldNormal) * 0.5 + 0.5, 1.0);
        return;
    }

    // ========== 涌潮波前场调试模式（BoreFrontField Debug） ==========

    // 模式 6：波前带符号距离可视化（signedDistance）
    // 蓝色区域 = 波前前方（尚未到达），红色区域 = 波前后方（已过去）
    // 亮白线 = 波前峰的当前位置（signedDistance ≈ 0）
    if(mode == 6){
        float signedDistance = fragBoreDebug0.x;
        float debugBandWidth = 5.0;  // 波前峰高亮带的宽度（米）

        // 在波前峰附近产生一条亮线，距离越近越亮
        float lengthMask = fragBoreDebug0.y;
        float frontLine =
            (
                1.0 -
                smoothstep(
                    0.0,
                    debugBandWidth,
                    abs(signedDistance)
                )
            ) * lengthMask;

        // 前方用蓝色表示，后方用橙红色表示
        vec3 sideColor = signedDistance > 0.0
            ? vec3(0.2, 0.4, 1.0)   // 前方：蓝
            : vec3(1.0, 0.25, 0.15); // 后方：橙红

        vec3 fieldColor =
            mix(
                vec3(0.04),
                sideColor,
                lengthMask
            );
        
        outColor = vec4(mix(fieldColor, vec3(1.0), frontLine), 1.0);
        return;
    }

    // 模式 7：波前长度淡入淡出掩码（lengthMask）
    // 白色 = 波前有效区域（mask = 1），黑色 = 波前两端之外（mask = 0）
    // 灰阶过渡 = 边缘淡出区域（由 edgeFadeFraction 控制）
    if(mode == 7){
        float lengthMask = fragBoreDebug0.y;
        outColor = vec4(vec3(lengthMask), 1.0);
        return;
    }

    // 模式 8：标准化波前线坐标（frontUClamped）
    // 绿色 → 红色渐变表示沿波前线从 0（起点）到 1（终点）的采样坐标
    // 用于验证 Front LUT 的纹理坐标映射是否正确
    if(mode == 8){
        float frontUClamped = fragBoreDebug0.z;
        outColor = vec4(frontUClamped, 1.0 - frontUClamped, 0.2, 1.0);
        return;
    }

    // 模式 9：局部波前法线方向可视化（localFrontNormal）
    // RGB 编码法线方向：红 = X 分量，绿 = Y 分量
    // 法线应始终指向涌潮推进方向，弯曲波前处法线方向会随 LUT 偏移而变化
    if(mode == 9){
        vec2 localFrontNormal = normalize(fragBoreDebug1.xy);
        outColor = vec4(localFrontNormal * 0.5 + 0.5, 0.5, 1.0);
        return;
    }

    // 模式 10：振幅乘数可视化（amplitudeMultiplier，Front LUT 的 G 通道）
    // 白色 = 振幅最大区域，黑色 = 振幅最低区域
    // 用于检查涌潮波峰高度沿波前线的空间分布是否符合设计
    if(mode == 10){
        float amplitudeMultiplier = fragBoreDebug0.w;
        outColor = vec4(vec3(amplitudeMultiplier), 1.0);
        return;
    }

    // 模式 11：泡沫乘数可视化（foamMultiplier，Front LUT 的 B 通道）
    // 白色 = 泡沫最强区域，黑色 = 无泡沫区域
    // 用于检查泡沫强度沿波前线的空间分布
    if(mode == 11){
        float foamMultiplier = fragBoreDebug1.z;
        outColor = vec4(vec3(foamMultiplier), 1.0);
        return;
    }

    // 模式 12：轮廓相位偏移可视化（profilePhaseOffset，Front LUT 的 A 通道）
    // 灰阶表示 Wave Profile 动画沿波前线的相位偏移量
    // 0 = 黑色（无偏移），1 = 白色（最大偏移）
    if(mode == 12){
        float profilePhaseOffset = fragBoreDebug1.w;
        outColor = vec4(vec3(profilePhaseOffset), 1.0);
        return;
    }

    // 模式 13：Wave Profile 的距离轴纹理坐标（profileU）
    // 绿→红渐变表示 signedDistance 映射到 [0, 1] 的纹理 U 坐标
    // 用于验证采样坐标映射是否正确
    if(mode == 13){
        float signedDistance = fragBoreDebug0.x;
        float profileHalfWidth = 30.0;           // 与配置中的 profileHalfWidth 一致
        float profileU =
            clamp(
                signedDistance /
                (2.0 * profileHalfWidth) +
                0.5,
                0.0,
                1.0
            );

        outColor = vec4(profileU, 1.0 - profileU, 0.2, 1.0);
        return;
    }

    // 模式 14：动画相位偏移（与 mode 12 相同，可能预留用于显示不同层级的相位）
    if(mode == 14){
        float profilePhaseOffset = fragBoreDebug1.w;
        outColor = vec4(vec3(profilePhaseOffset), 1.0);
        return;
    }

    // ===== Wave Profile 位移纹理采样结果可视化 =====

    // 模式 15：前向水平位移（Wave Profile 位移纹理的 R 通道）
    // 灰阶 = forward displacement
    // 中间灰为 0，亮 = 正位移（向前推），暗 = 负位移（向后拉/回流）
    if(mode == 15){
        float forward = fragBoreProfile0.x;
        outColor = vec4(vec3(0.5 + forward * 0.1), 1.0);
        return;
    }

    // 模式 16：向上垂直位移 / 波高（Wave Profile 位移纹理的 G 通道）
    // 灰阶 = upward displacement
    // 中间灰为 0，亮 = 波峰（正高度），暗 = 波谷（负高度）
    if(mode == 16){
        float upward = fragBoreProfile0.y;
        outColor = vec4(vec3(0.5 + upward * 0.1), 1.0);
        return;
    }

    // 模式 17：泡沫源（Wave Profile 位移纹理的 B 通道）
    // 灰阶 = foamSource
    // 亮 = 泡沫最强区域，暗 = 无泡沫
    if(mode == 17){
        float foamSource = fragBoreProfile0.z;
        outColor = vec4(vec3(foamSource), 1.0);
        return;
    }

    // 模式 18：浪尖掩码（Wave Profile 位移纹理的 A 通道）
    // 灰阶 = crestMask
    // 亮 = 浪尖区域，暗 = 非浪尖区域
    if(mode == 18){
        float crestMask = fragBoreProfile0.w;
        outColor = vec4(vec3(crestMask), 1.0);
        return;
    }

    // ===== Wave Profile 导数纹理采样结果可视化 =====

    // 模式 19：向上位移的导数 dUpward/ds（Wave Profile 导数纹理的 G 通道）
    // 灰阶 = 波高随距离的变化率（坡度）
    // 中间灰为 0，亮 = 正坡度（水面上升），暗 = 负坡度（水面下降）
    if(mode == 19){
        float dUpwardDs = fragBoreProfile1.y;
        outColor = vec4(vec3(0.5 + dUpwardDs * 0.5), 1.0);
        return;
    }

    // 模式 20：流速（Wave Profile 导数纹理的 B 通道）
    // 灰阶 = flowSpeed
    // 亮 = 高流速（波峰处增强），暗 = 低流速
    // 除以 12.0 是为了将典型流速范围映射到 [0, 1]
    if(mode == 20){
        float flowSpeed = fragBoreProfile1.z;
        outColor = vec4(vec3(flowSpeed / 12.0), 1.0);
        return;
    }

    // 模式 21：破碎权重（Wave Profile 导数纹理的 A 通道）
    // 灰阶 = breakingWeight
    // 亮 = 高破碎概率，暗 = 低破碎概率
    if(mode == 21){
        float breakingWeight = fragBoreProfile1.w;
        outColor = vec4(vec3(breakingWeight), 1.0);
        return;
    }

    // ===== 合成结果与法线调试 =====

    // 模式 22：FFT suppression weights
    // R = shortWeight, G = midWeight, B = longWeight
    // crestMask 区域中短波应压得最强，长波保留最多
    if(mode == 22){
        outColor = vec4(
            fragComposition.x,
            fragComposition.y,
            fragComposition.z,
            1.0
        );
        return;
    }

    // 模式 23：最终合成坡度可视化（fragSlopeDebug.xy）
    // 红 = slopeX，蓝 = slopeZ
    // 用于检查 FFT + Bore 叠加后的总坡度方向
    if(mode == 23){
        outColor = vec4(SignedColor(fragSlopeDebug.zw, 0.25), 1.0);
        return;
    }

    // 模式 24：最终世界空间法线可视化
    // RGB = 法线方向映射到 [0, 1]
    // 绿 = 朝上，红/蓝 = 倾斜
    if(mode == 24){
        outColor = vec4(normalize(fragWorldNormal) * 0.5 + 0.5, 1.0);
        return;
    }

    // 模式 25：潮后掩码可视化（fragComposition.w）
    // 灰阶 = backMask
    // 亮 = 波前后方（已淹没），暗 = 波前前方（未到达）
    // 用于检查潮后水位抬升的空间范围
    if(mode == 25){
        float backMask = fragComposition.w;
        outColor = vec4(vec3(backMask), 1.0);
        return;
    }

    // 模式 26：最终位移
    // RGB = 实际加到 worldPosition 上的 X/Y/Z 位移
    if(mode == 26){
        outColor = vec4(
            0.5 + fragFinalDisplacement.x * 0.05,
            0.5 + fragFinalDisplacement.y * 0.05,
            0.5 + fragFinalDisplacement.z * 0.05,
            1.0
        );
        return;
    }

    // 模式 27：Profile 泡沫源可视化（fragFoamSourceData.r）
    // 灰阶 = profileFoam，仅来自涌潮剖面位移纹理的泡沫源
    if(mode == 27){
        outColor = vec4(vec3(profileFoam), 1.0);
        return;
    }

    // 模式 28：FFT Jacobian 泡沫源可视化（fragFoamSourceData.g）
    // 灰阶 = fftJacobianFoam，由 FFT 波浪的 Jacobian 破碎判定产生
    if(mode == 28){
        outColor = vec4(vec3(fftJacobianFoam), 1.0);
        return;
    }

    // 模式 29：坡度泡沫源可视化（fragFoamSourceData.b）
    // 灰阶 = slopeFoam，由水面总坡度过大产生的泡沫
    if(mode == 29){
        outColor = vec4(vec3(slopeFoam), 1.0);
        return;
    }

    // 模式 30：Bore 破碎泡沫源可视化（fragFoamSourceData.a）
    // 灰阶 = boreBreakingFoam，来自涌潮破碎权重
    if(mode == 30){
        outColor = vec4(vec3(boreBreakingFoam), 1.0);
        return;
    }

    // 模式 31：合并泡沫源可视化（foamSource）
    // 灰阶 = 最终合成的泡沫源强度（取上述四种源的最大值）
    if(mode == 31){
        outColor = vec4(vec3(foamSource), 1.0);
        return;
    }

    // 模式 32：三相位泡沫细节 Phase 0 的覆盖率
    // 灰阶 = detail0.coverage，用于观察单个相位的泡沫纹理
    if(mode == 32){
        outColor = vec4(vec3(detail0.coverage), 1.0);
        return;
    }

    // 模式 33：三相位泡沫细节 Phase 1 的覆盖率
    if(mode == 33){
        outColor = vec4(vec3(detail1.coverage), 1.0);
        return;
    }

    // 模式 34：三相位泡沫细节 Phase 2 的覆盖率
    if(mode == 34){
        outColor = vec4(vec3(detail2.coverage), 1.0);
        return;
    }

    // 模式 35：三相位混合权重可视化
    // R = phase0 权重，G = phase1 权重，B = phase2 权重
    if(mode == 35){
        outColor = vec4(w0, w1, w2, 1.0);
        return;
    }

    // 模式 36：泡沫覆盖率可视化（foamCoverage）
    // 灰阶 = 经过细节纹理调制和阈值处理后的视觉泡沫强度
    if(mode == 36){
        outColor = vec4(vec3(foamCoverage), 1.0);
        return;
    }

    // 模式 37：泡沫流速可视化
    // RGB 编码泡沫平流速度向量（范围 0~1），用于检查流向是否正确
    if(mode == 37){
        outColor = vec4(
            foamVelocity * 0.05 + 0.5,
            0.0,
            1.0
        );
        return;
    }

    // 模式 38：状态型泡沫可视化（stateFoam）
    // 灰阶 = 从 Ping‑Pong 泡沫状态图中读取的历史泡沫浓度
    if(mode == 38){
        outColor = vec4(vec3(stateFoam), 1.0);
        return;
    }

    // 模式 39：最终泡沫混合结果可视化（finalFoam）
    // 灰阶 = 三相位泡沫覆盖率与状态型泡沫的混合输出
    if(mode == 39){
        outColor = vec4(vec3(finalFoam), 1.0);
        return;
    }
    
    // 模式 40：河流流向可视化（riverFlowTexture.rg）
    // RGB 编码流向向量（Flow Map 的 R/G 通道），红 = 流向 X 分量，绿 = 流向 Z 分量
    // 用于检查河流场纹理的流向是否正确、是否随河道弯曲而平滑变化
    if(mode == 40){
        outColor =
            vec4(
                fragRiverFlow.rg * 0.5 + 0.5,   // 将 [-1,1] 映射到 [0,1] 颜色空间
                0.5,
                1.0
            );
        return;
    }

    // 模式 41：沿河归一化进度可视化（riverCoordinateTexture.r）
    // 灰度图，0 = 黑色（入海口），1 = 白色（河道末端）
    // 用于检查 Coordinate Map 的进度通道是否从 0 到 1 连续渐变
    if(mode == 41){
        outColor =
            vec4(
                vec3(fragRiverCoord.r),     // 归一化进度 [0, 1]
                1.0
            );
        return;
    }

    // 模式 42：横向归一化坐标可视化（riverCoordinateTexture.g）
    // 红蓝过渡，蓝 = 左岸（-1），灰 = 中轴线（0），红 = 右岸（+1）
    // 用于检查 Coordinate Map 的横向坐标是否在河道宽度内正确映射
    if(mode == 42){
        outColor =
            vec4(
                fragRiverCoord.g * 0.5 + 0.5,   // 将 [-1,1] 映射到 [0,1]
                0.5,
                0.5,
                1.0
            );
        return;
    }

    // 模式 43：水域掩码可视化（riverFlowTexture.a）
    // 灰度图，亮 = 河道内（mask = 1），暗 = 岸上（mask = 0），过渡区为灰色
    // 用于检查 Flow Map 的水域掩码是否正确标记了河道范围
    if(mode == 43){
        outColor =
            vec4(
                vec3(fragRiverFlow.a),      // 水域掩码 [0, 1]
                1.0
            );
        return;
    }

    // 到岸的有符号距离：>0 河内，<0 岸上 河道中心亮、岸线处灰(0.5)、岸上暗 → SDF 正确
    // wetnessBase：河内=1，岸上在 wetRunup 内线性淡出 河内白、岸上渐黑 → wetness 基底
    // sand：岸线两侧一段范围内为 1 岸线一条亮带 → sand
    // terrainHeight 占位：岸上按坡度抬升，河内=0（B 步换成真实 heightmap）岸上渐亮的坡 → 占位地形高度
    if(mode == 44){ outColor = vec4(vec3(clamp(fragShore.r / 200.0 + 0.5, 0.0, 1.0)), 1.0); return; }
    if(mode == 45){ outColor = vec4(vec3(fragShore.g), 1.0); return; } // wetnessBase
    if(mode == 46){ outColor = vec4(vec3(fragShore.b), 1.0); return; } // sand
    if(mode == 47){ outColor = vec4(vec3(fragShore.a / 12.0), 1.0); return; } // terrainHeight

    // 默认：显示 UV 坐标（红绿通道）
    outColor = vec4(fragUV, 0.0, 1.0);
}