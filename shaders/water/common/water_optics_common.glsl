// cosTheta：视线方向与表面法线的夹角余弦（即 dot(viewDir, normal) 的绝对值）。
// power：菲涅尔指数，控制反射强度随角度变化的锐利程度。
// 当视线垂直水面（cosTheta ≈ 1）时，反射率接近 0，水面清澈；
// 当视线贴近水面（cosTheta ≈ 0）时，反射率接近 1，水面如镜
float WaterFresnel(
    float NdotV,
    float F0
){
    float clampedNdotV =
        clamp(NdotV, 0.0, 1.0);

    return
        F0 +
        (1.0 - F0) *
        pow(
            1.0 - clampedNdotV,
            5.0
        );
}

// 简易天空色采样
// 将视线高度映射为 [0, 1] 的插值因子，在天顶颜色（暗蓝）和地平线颜色（亮灰蓝）之间做线性混合。
// 这是一种最简单的程序化天空，不需要采样天空盒纹理
vec3 SimpleSkyColor(vec3 viewDir)
{
    float t =
        clamp(viewDir.y * 0.5 + 0.5, 0.0, 1.0);

    return mix(
        vec3(0.45, 0.58, 0.72), // 地平线附近的灰蓝色
        vec3(0.08, 0.18, 0.32), // 天顶的暗蓝色
        1.0 - t
    );
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