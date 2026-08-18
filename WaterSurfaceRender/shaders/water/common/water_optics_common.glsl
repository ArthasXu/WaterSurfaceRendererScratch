// cosTheta：视线方向与表面法线的夹角余弦（即 dot(viewDir, normal) 的绝对值）。
// power：菲涅尔指数，控制反射强度随角度变化的锐利程度。
// 当视线垂直水面（cosTheta ≈ 1）时，反射率接近 0，水面清澈；
// 当视线贴近水面（cosTheta ≈ 0）时，反射率接近 1，水面如镜
float WaterFresnel(
    float NdotV,
    float F0
){
    // 钳位到有效范围
    float clampedNdotV = clamp(NdotV, 0.0, 1.0);

    // Schlick 近似：F0 + (1 - F0) * (1 - cosθ)^5
    return
        F0 +
        (1.0 - F0) *
        pow(
            1.0 - clampedNdotV,
            5.0
        );
}

// 带太阳的程序化天空：dir=视线方向, sunDir=太阳方向
vec3 SkyColor(vec3 dir, vec3 sunDir)
{
    dir = normalize(dir);
    float up = clamp(dir.y * 0.5 + 0.5, 0.0, 1.0);

    vec3 horizon = vec3(0.75, 0.85, 0.92); // 地平线：浅青白
    vec3 zenith  = vec3(0.20, 0.45, 0.78); // 天顶：饱和蓝
    vec3 sky = mix(horizon, zenith, pow(up, 0.55));

    // 太阳盘 + 两级光晕
    float sd   = clamp(dot(dir, normalize(sunDir)), 0.0, 1.0);
    float disk = smoothstep(0.9992, 0.9998, sd);
    float glow = pow(sd, 400.0) * 0.7 + pow(sd, 18.0) * 0.18;
    vec3 sunCol = vec3(1.0, 0.96, 0.86);
    sky += sunCol * (disk * 10.0 + glow);

    // 地平线大气增白
    sky = mix(sky, vec3(0.88, 0.92, 0.96), pow(1.0 - up, 6.0) * 0.5);
    return sky;
}

// 兼容旧调用（其他 stage 仍用无太阳版）
vec3 SimpleSkyColor(vec3 viewDir)
{
    return SkyColor(viewDir, normalize(vec3(0.4, 0.85, 0.3)));
}

// color：原始像素颜色（水面已着色的结果）。
// distanceToCamera：当前片段到摄像机的世界空间距离。
// fogColor：雾的颜色（通常取天空色或大气色）。
// fogStart / fogEnd：雾的起始和完全覆盖距离。
// 原理：在 [fogStart, fogEnd] 区间内用 smoothstep 做平滑过渡，
// 将原始颜色逐渐替换为雾的颜色，产生“远处物体隐没在雾中”的效果。
// 在水体中的作用：为远处水面增加大气透视感，让海天交界处自然融合，避免生硬的几何边缘，同时增强场景深度感。
vec3 ApplyDistanceFog(
    vec3 color,
    float distanceToCamera,
    vec3 fogColor,
    float fogStart,
    float fogEnd
){
    float fog =
        smoothstep(
            fogStart,
            fogEnd,
            distanceToCamera
        );

    return mix(
        color,
        fogColor,
        fog
    );
}

// GGX 法线分布函数（NDF）
// 描述在给定的粗糙度下，有多少比例的微观表面法线恰好指向了半角向量 H。这个比例决定了高光的强度和形状
// NoH      ：法线与半角向量的夹角余弦
// roughness：表面粗糙度（0=完全光滑，1=极端粗糙）
float D_GGX(float NoH, float roughness)
{
    // 粗糙度平方，让参数与视觉感知更线性
    float a  = roughness * roughness;
    float a2 = a * a;

    // 标准 GGX/Trowbridge‑Reitz 公式
    float d  = NoH * NoH * (a2 - 1.0) + 1.0;

    // 分母是 π * d²，除以π保证能量守恒
    return a2 / max(3.14159265 * d * d, 1e-7);
}

// Smith GGX 可见性函数
// 和 D_GGX 配合使用，修正光线在掠射角时被粗糙表面自身遮挡导致的亮度衰减
// NoV      ：视线与法线的夹角余弦
// NoL      ：光线与法线的夹角余弦
// roughness：表面粗糙度
float V_SmithGGX(float NoV, float NoL, float roughness)
{
    float a  = roughness * roughness;
    // k 是粗糙度相关的修正因子，这里用的是 GGX 的经典简化
    float k  = a * 0.5;

    // 分别计算视线方向和光线方向的几何遮蔽
    float gv = NoL * (NoV * (1.0 - k) + k);
    float gl = NoV * (NoL * (1.0 - k) + k);

    // 返回可见性，分母防止除零
    return 0.5 / max(gv + gl, 1e-5);
}

// ===== FF MF_ImposibleNormalFix（平滑版）=====
// V = 从像素指向相机。R.y<0 表示反射射向水面以下（几何不可能，背朝相机的浪面）。
// 注意：原式用 clamp(-R.y) 在 R.y=0 处一阶不连续，而 R.y 的零值集在起伏水面上
// 是一族沿波形的曲线，会被渲染成移动的亮白细线。改用 smoothstep 平滑起始。
vec3 ImpossibleNormalFix(vec3 N, vec3 V, float strength)
{
    vec3  R = reflect(-V, N);
    const float softness = 0.35;                     // 过渡带宽度，越大越平滑
    float impossible = smoothstep(0.0, softness, -R.y);
    return normalize(N + impossible * strength * V);
}

// ===== FF MF_Fresnel =====
// saturate(bias + scale * pow(1 - dot(V,N), power))
// 与 Schlick 的区别：power 可调。FF 默认 9.0，把反射集中在接近水平的视角，
// 俯视时水面清澈，只有远处/掠射处强反射。
float FluxFresnel(float bias, float scale, float power, vec3 N, vec3 V)
{
    float f = 1.0 - dot(V, N);
    f = pow(max(f, 0.0), power);
    return clamp(bias + f * scale, 0.0, 1.0);
}

// ===== FF MF_FluidWaterLayer 的 Specular_B_Y：地平线高光衰减 =====
// 有效高光距离 ≈ horizonDistance + 相机相对水面高度 × horizonOffset。
// 超出后只保留 horizonFloor 的底噪。这是远景水面不糊成白片的关键。
float FluxSpecularHorizon(
    vec3 posWS, vec3 camPosWS,
    float horizonDistance, float horizonOffset, float horizonFloor
){
    vec3  toCam = posWS - camPosWS;
    float d = horizonDistance - toCam.y * horizonOffset;
    float f = (d - length(toCam)) / max(d * 0.9, 1.0e-4);
    f = clamp(f, 0.0, 1.0);
    f = f * f;
    return clamp(f + horizonFloor, 0.0, 1.0);
}

// ===== FF MF_FluidWaterLayer 的 Roughness =====
// 1) NoH≈1 处加极小粗糙度，柔化太阳光斑硬边
// 2) 掠射角按 (1-NoV)^5 加粗糙度 → 远处高光变宽变暗
// 3) 下限保证正常视角是锐利镜面
float FluxWaterRoughness(float NoH, float NoV, float roughFromFresnel, float roughMin)
{
    const float div = 0.997;
    float r = clamp((NoH - div) / (1.0 - div), 0.0, 1.0) * 0.007;
    r += roughFromFresnel * pow(clamp(1.0 - NoV, 0.0, 1.0), 5.0);
    return max(r, roughMin);
}

// ===== FF MF_FluidScattering（Cheap 版）=====
// 视线越贴近水面、越偏离天顶 → 浪面越"透光发亮"。
// 输出喂给 MF_SingleLayerWater 的 WaveScattering：削弱吸收、增强散射。
float FluxCheapScattering(
    vec3 vertexNormal, vec3 pixelNormal, vec3 V,
    float details, float power, float scale
){
    float vertexNoV = dot(vertexNormal, V);
    float pixelNoV  = dot(pixelNormal, V);
    float s = mix(vertexNoV, abs(pixelNoV), details);
    s -= V.y;                       // FF: -= CameraDir.g
    s = clamp(s, 0.0, 1.0);
    s = pow(s, power);
    return clamp(s * scale, 0.0, 1.0);
}