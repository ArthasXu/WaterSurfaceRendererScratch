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