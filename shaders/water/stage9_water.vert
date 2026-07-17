#version 450

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inUV;

layout(set = 0, binding = 0) uniform CameraUBO
{
    mat4 model;
    mat4 view;
    mat4 projection;
    ivec4 debug;
} camera;

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
layout(location = 11) out vec4 fragFinalDisplacement;

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

void main(){
    // 将顶点从模型空间变换到世界空间，得到未变形的水面基础位置
    vec3 baseWorldPosition =
        (camera.model * vec4(inPosition, 1.0)).xyz;

    vec2 boreDirection =
        normalize(bore.directionLengthFade.xy);

    vec2 boreTangent =
        vec2(-boreDirection.y, boreDirection.x);

    vec2 boreRelative =
        baseWorldPosition.xz -
        bore.originSpeedTime.xy;

    float alongFront =
        dot(boreRelative, boreTangent);

    float crossFront =
        dot(boreRelative, boreDirection);

    float frontLength =
        bore.directionLengthFade.z;

    float frontU =
        alongFront / frontLength + 0.5;

    float frontUClamped =
        clamp(frontU, 0.0, 1.0);

    float edgeFade =
        bore.directionLengthFade.w;

    float lengthMask =
        smoothstep(0.0, edgeFade, frontU) *
        (1.0 - smoothstep(1.0 - edgeFade, 1.0, frontU));

    vec4 frontParams =
        textureLod(
            frontParameterLUT,
            vec2(frontUClamped, 0.5),
            0.0
        );

    vec4 derivativeData =
        textureLod(
            frontDerivativeLUT,
            vec2(frontUClamped, 0.5),
            0.0
        );

    float useLUT =
        bore.lutInfo.z;

    float offsetMeters =
        mix(0.0, frontParams.r, useLUT);

    float frontPosition =
        bore.motionDebug.x +
        bore.originSpeedTime.z *
        bore.originSpeedTime.w +
        offsetMeters;

    float signedDistance =
        crossFront - frontPosition;

    float dOffsetDu =
        mix(0.0, derivativeData.r, useLUT);

    float dOffsetDa =
        dOffsetDu / frontLength;

    vec2 localFrontNormal =
        normalize(
            boreDirection -
            dOffsetDa * boreTangent
        );

    float amplitudeMultiplier =
        mix(1.0, frontParams.g, useLUT);

    float foamMultiplier =
        mix(1.0, frontParams.b, useLUT);

    float profilePhaseOffset =
        mix(0.0, frontParams.a, useLUT);

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

    float profileVRaw =
        profileConfig.animation.x /
        profileDuration +
        profilePhaseOffset *
        profileConfig.animation.y;

    bool looping =
        profileConfig.animation.z > 0.5;

    float profileV =
        looping
        ? fract(profileVRaw)
        : clamp(profileVRaw, 0.0, 1.0);

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

    // 涌潮最终强度：由多个开关和空间掩码连乘决定
    // 任意一个为 0，涌潮就在该点不生效
    float boreStrength =
        boreEnabled *               // 用户是否开启涌潮效果（键盘 B 键）
        profileEnabled *            // Wave Profile 是否启用
        activeRegionMask *          // 当前点是否在涌潮区域内
        lengthMask *                // 波前两端淡入淡出掩码
        amplitudeMultiplier *       // Front LUT 的振幅乘数（G 通道）
        globalAmplitude;            // 全局振幅缩放

    // 浪尖掩码：标记当前点是否位于涌潮波峰附近
    // 用于后续抑制背景 FFT 波浪，避免背景波浪破坏潮头形状
    float crestMask =
        boreProfile.a *             // Wave Profile 的浪尖掩码（A 通道）
        lengthMask *                // 波前两端淡入淡出
        boreEnabled *               // 涌潮开关
        profileEnabled *            // 剖面开关
        activeRegionMask;           // 区域掩码

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
        boreStrength;               // 涌潮总强度

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
        profileConfig.domain.y *    // 水位抬升高度
        dBackMaskDs *               // 掩码变化率
        boreStrength;

    // 前向水平位移对距离的导数（Wave Profile 导数纹理的 R 通道）
    float dForwardDs =
        boreDerivative.r *          // d(forward)/ds
        forwardScale *              // 缩放
        boreStrength;

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
        (
            dUpwardDs +             // 波高本身的坡度
            dWaterRiseDs            // 潮后水位抬升的坡度
        ) /
        denominator;

    // 坡度沿局部波前法线方向
    vec2 boreSlope =
        effectiveBoreSlope *
        localFrontNormal;

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

    // ===== 第六步：合成最终坡度并重建法线 =====

    // 总坡度 = FFT 坡度 + 涌潮坡度
    vec2 totalSlope =
        fftSlope *                  // FFT 斜率（三个 Cascade 加权后）
        water.simulation.z +        // 法线扰动强度
        boreSlope;                  // 涌潮坡度

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

    // MVP 变换到裁剪空间，输出最终顶点位置
    gl_Position =
        camera.projection *
        camera.view *
        vec4(worldPosition, 1.0);
}