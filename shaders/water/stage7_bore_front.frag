#version 450

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

    // 模式 0：基础漫反射光照
    if(mode == 0){
        // 主光源方向（斜上方）
        vec3 lightDir = normalize(vec3(0.4, 1.0, 0.25));
        // 兰伯特漫反射因子
        float ndotl = clamp(dot(normalize(fragWorldNormal), lightDir), 0.0, 1.0);
        // 基础海水颜色
        vec3 baseColor = vec3(0.02, 0.20, 0.32);
        // 环境光 + 漫反射混合
        vec3 litColor = baseColor * (0.35 + 0.65 * ndotl);
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
        float frontLine = 1.0 - smoothstep(0.0, debugBandWidth, abs(signedDistance));

        // 前方用蓝色表示，后方用橙红色表示
        vec3 sideColor = signedDistance > 0.0
            ? vec3(0.2, 0.4, 1.0)   // 前方：蓝
            : vec3(1.0, 0.25, 0.15); // 后方：橙红

        outColor = vec4(mix(sideColor, vec3(1.0), frontLine), 1.0);
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

    // 默认：显示 UV 坐标（红绿通道）
    outColor = vec4(fragUV, 0.0, 1.0);
}