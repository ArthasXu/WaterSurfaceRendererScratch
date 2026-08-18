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

layout(location = 0) out vec3 fragWorldPosition;
layout(location = 1) out vec3 fragWorldNormal;
layout(location = 2) out vec2 fragUV;
layout(location = 3) out vec4 fragDisplacement;
layout(location = 4) out vec4 fragNormalAux;

void AccumulateCascade(
    sampler2D displacementTexture,
    sampler2D normalAuxTexture,
    vec2 worldXZ,
    float patchLength,
    float amplitudeScale,
    inout vec4 displacementSum,
    inout vec2 slopeSum,
    inout float breakingHint
){
    vec2 uv = fract(worldXZ / patchLength); // 计算纹理坐标

    vec4 displacement = texture(displacementTexture, uv); // 获取纹理坐标处的位移
    vec4 normalAux = texture(normalAuxTexture, uv); // 获取纹理坐标处的法线辅助信息

    displacementSum.xyz += displacement.xyz * amplitudeScale; // 缩放并累加位移
    slopeSum += normalAux.xy * amplitudeScale; // 缩放并累加斜率

    // 根据当前层的 Jacobian 计算破碎程度（clamp(1.0 - displacement.a, 0.0, 1.0)），然后取当前最大值存入 breakingHint
    float cascadeBreaking = clamp(1.0 - displacement.a, 0.0, 1.0);
    breakingHint = max(breakingHint, cascadeBreaking); 
}

void main(){
    // 将顶点从模型空间变换到世界空间，得到未变形的水面基础位置
    vec3 baseWorldPosition =
        (camera.model * vec4(inPosition, 1.0)).xyz;

    // 从位移纹理中获取当前顶点的水平位移和高度
    // jacobian = 1：网格局部面积不变，水面只是整体平移或转动。
    // jacobian > 1：网格局部被拉伸，面积变大。
    // 0 < jacobian < 1：网格局部被压缩，面积变小，波浪开始堆积。
    // jacobian < 0：网格发生了翻转（fold-over），即波峰过度尖锐导致的水面自交叉。
    // jacobian 越小（尤其是负值），说明该处波浪越陡峭、能量越集中，越应该出现白浪
    vec4 displacement = vec4(0.0);
    vec2 slope = vec2(0.0);
    float breakingHint = 0.0;

    // 累加第0层（低频/长波）的位移、斜率、泡沫判据
    AccumulateCascade(
        fftDisplacement0,           // 第0层位移纹理
        fftNormalAux0,              // 第0层法线辅助纹理
        baseWorldPosition.xz,       // 世界空间水平坐标
        water.patchLengths.x,       // 第0层补丁边长
        water.amplitudeScales.x,    // 第0层振幅缩放
        displacement,               // inout：累加的位移 (dispX, height, dispZ, …)
        slope,                      // inout：累加的斜率 (slopeX, slopeZ)
        breakingHint                // inout：泡沫/破碎判据（取最大值）
    );

    // 累加第1层（中频/中波）的位移、斜率、泡沫判据
    AccumulateCascade(
        fftDisplacement1,
        fftNormalAux1,
        baseWorldPosition.xz,
        water.patchLengths.y,
        water.amplitudeScales.y,
        displacement,
        slope,
        breakingHint
    );

    // 累加第2层（高频/短波）的位移、斜率、泡沫判据
    AccumulateCascade(
        fftDisplacement2,
        fftNormalAux2,
        baseWorldPosition.xz,
        water.patchLengths.z,
        water.amplitudeScales.z,
        displacement,
        slope,
        breakingHint
    );

    // 将泡沫判据存入位移向量的 w 分量，用于后续片段着色器叠加泡沫
    displacement.w = 1.0 - breakingHint;

    // 计算用于可视化或调试的 FFT UV（这里取中层补丁的 UV）
    vec2 fftUV =
        fract(baseWorldPosition.xz / water.patchLengths.y);

    // 从基础位置开始构造最终世界坐标
    vec3 worldPosition = baseWorldPosition;
    // 应用水平位移（乘以 choppy 强度并放大 5 倍以增强视觉效果）
    worldPosition.xz += displacement.xz * water.simulation.y * 5.0;
    // 应用垂直位移（波浪高度放大 10 倍）
    worldPosition.y += displacement.y * 10.0;

    // 根据控制参数缩放斜率，用于最终法线重建
    slope *= water.simulation.z;
    
    // 根据斜率重建世界空间法线：原法线为 (0,1,0)，经斜率扰动后归一化
    vec3 worldNormal = normalize(vec3(
        -slope.x,
         1.0,
        -slope.y
    ));

    // 将世界空间的位置、法线、UV 以及调试数据传递给片段着色器
    fragWorldPosition = worldPosition;
    fragWorldNormal = worldNormal;
    fragUV = fftUV;
    fragDisplacement = displacement;
    fragNormalAux = vec4(slope, 0.0, 0.0);

    // MVP 变换到裁剪空间，输出最终顶点位置
    gl_Position =
        camera.projection *
        camera.view *
        vec4(worldPosition, 1.0);
}