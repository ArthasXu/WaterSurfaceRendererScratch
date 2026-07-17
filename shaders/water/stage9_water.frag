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

// 摄像机 UBO（绑定 set=0, binding=0）
layout(set = 0, binding = 0) uniform CameraUBO {
    mat4 model;         // 物体自身变换（平移、旋转、缩放）
    mat4 view;          // 相机视图变换
    mat4 projection;    // 透视投影
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

void main()
{
    int mode = camera.debug.x;

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

    vec2 detailNormal =
        normalize(
            detail0.normalXZ * w0 +
            detail1.normalXZ * w1 +
            detail2.normalXZ * w2
        );

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
        fragWorldPosition.xz /
        vec2(256.0) +
        vec2(0.5);

    float stateFoam =
        foam.runtime.x < 0.5
        ? texture(foamState0, stateUV).r
        : texture(foamState1, stateUV).r;

    float finalFoam =
        mix(
            foamCoverage,
            stateFoam,
            foam.appearance.w
        );

    // 模式 0：完整水体着色（光照 + 反射 + 高光 + 泡沫 + 雾）
    if(mode == 0){
        // 归一化世界空间法线，确保光照计算正确
        vec3 normal = normalize(fragWorldNormal);

        // 视线方向：从片段指向摄像机（世界空间）
        vec3 viewDir = normalize(-fragWorldPosition);

        // 太阳方向（由 UBO 传入，归一化）
        vec3 sunDir = normalize(material.lightParams.xyz);

        // 兰伯特漫反射因子：法线与太阳方向夹角越小越亮
        float ndotl = clamp(dot(normal, sunDir), 0.0, 1.0);

        // 高度混合因子：根据片段世界 Y 坐标决定浅水/深水颜色混合
        // Y 越高（越接近水面波峰），浅水色越明显；Y 越低（波谷），深水色越重
        float heightFactor = clamp(fragWorldPosition.y * 0.04 + 0.5, 0.0, 1.0);

        // 根据高度因子混合深浅水颜色，得到基础水色
        vec3 waterColor = mix(material.deepColor.rgb, material.shallowColor.rgb, heightFactor);

        // 混入泥沙颜色，模拟浑浊水体（如河口、风暴后）
        waterColor = mix(waterColor, material.sedimentColor.rgb, material.opticalParams.w);

        // 菲涅尔反射率：使用 Schlick 近似，power 由 UBO 传入
        float fresnel = SchlickFresnel(clamp(dot(normal, -viewDir), 0.0, 1.0), material.opticalParams.x);

        // 反射天空颜色：根据反射视线方向采样程序化天空
        vec3 reflectedSky = SimpleSkyColor(reflect(viewDir, normal));

        // 漫反射项：水色 ×（环境光 30% + 太阳漫反射 70%）
        vec3 diffuse = waterColor * (0.30 + 0.70 * ndotl);

        // Blinn-Phong 半角向量，用于计算太阳高光
        vec3 halfVector = normalize(sunDir - viewDir);

        // 太阳高光强度（Blinn-Phong 模型），指数 96 控制高光锐度
        float specular = pow(clamp(dot(normal, halfVector), 0.0, 1.0), 96.0) * material.lightParams.w;

        // 菲涅尔混合：将漫反射与天空反射按菲涅尔比例混合
        vec3 litColor = mix(diffuse, reflectedSky, fresnel * material.opticalParams.y);

        // 叠加暖色太阳高光
        litColor += vec3(1.0, 0.88, 0.62) * specular;

        // 泡沫颜色（偏冷白）
        vec3 foamColor = vec3(0.92, 0.96, 0.92);

        // 混合泡沫：将已着色的水面与泡沫颜色按 finalFoam 强度混合
        litColor = mix(litColor, foamColor, finalFoam);

        // 计算到摄像机的距离，用于雾效
        float distanceToCamera = length(fragWorldPosition);

        // 距离雾效：远处水面逐渐融入雾色，增强大气透视
        litColor = ApplyDistanceFog(
            litColor, distanceToCamera,
            vec3(0.50, 0.58, 0.62), // 雾色（灰蓝）
            material.fogParams.x,    // 雾起始距离
            material.fogParams.y     // 雾完全覆盖距离
        );

        outColor = vec4(litColor, 1.0);
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

    if(mode == 27){
        outColor = vec4(vec3(profileFoam), 1.0);
        return;
    }

    if(mode == 28){
        outColor = vec4(vec3(fftJacobianFoam), 1.0);
        return;
    }

    if(mode == 29){
        outColor = vec4(vec3(slopeFoam), 1.0);
        return;
    }

    if(mode == 30){
        outColor = vec4(vec3(boreBreakingFoam), 1.0);
        return;
    }

    if(mode == 31){
        outColor = vec4(vec3(foamSource), 1.0);
        return;
    }

    if(mode == 32){
        outColor = vec4(vec3(detail0.coverage), 1.0);
        return;
    }

    if(mode == 33){
        outColor = vec4(vec3(detail1.coverage), 1.0);
        return;
    }

    if(mode == 34){
        outColor = vec4(vec3(detail2.coverage), 1.0);
        return;
    }

    if(mode == 35){
        outColor = vec4(w0, w1, w2, 1.0);
        return;
    }

    if(mode == 36){
        outColor = vec4(vec3(foamCoverage), 1.0);
        return;
    }

    if(mode == 37){
        outColor = vec4(
            foamVelocity * 0.05 + 0.5,
            0.0,
            1.0
        );
        return;
    }

    if(mode == 38){
        outColor = vec4(vec3(stateFoam), 1.0);
        return;
    }

    if(mode == 39){
        outColor = vec4(vec3(finalFoam), 1.0);
        return;
    }

    // 默认：显示 UV 坐标（红绿通道）
    outColor = vec4(fragUV, 0.0, 1.0);
}