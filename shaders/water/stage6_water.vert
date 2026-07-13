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

layout(location = 0) out vec3 fragWorldPosition;
layout(location = 1) out vec3 fragWorldNormal;
layout(location = 2) out vec2 fragUV;
layout(location = 3) out vec4 fragDisplacement;
layout(location = 4) out vec4 fragNormalAux;

void main(){
    // 将顶点从模型空间变换到世界空间，得到未变形的水面基础位置
    vec3 baseWorldPosition =
        (camera.model * vec4(inPosition, 1.0)).xyz;

    // 计算纹理坐标：利用世界坐标周期性重复采样位移图，patchLengths.x 为补丁边长
    vec2 fftUV =
        fract(baseWorldPosition.xz / water.patchLengths.x);

    // 从位移纹理中获取当前顶点的水平位移和高度
    // jacobian = 1：网格局部面积不变，水面只是整体平移或转动。
    // jacobian > 1：网格局部被拉伸，面积变大。
    // 0 < jacobian < 1：网格局部被压缩，面积变小，波浪开始堆积。
    // jacobian < 0：网格发生了翻转（fold-over），即波峰过度尖锐导致的水面自交叉。
    // jacobian 越小（尤其是负值），说明该处波浪越陡峭、能量越集中，越应该出现白浪
    vec4 displacement =
        texture(fftDisplacement0, fftUV);   // (dispX, height, dispZ, jacobian)

    // 从法线辅助纹理中获取斜率信息
    vec4 normalAux =
        texture(fftNormalAux0, fftUV);      // (slopeX, slopeZ, dDxdx, dDzdz)

    // 应用水平位移：simulation.y 控制 choppy 强度
    vec3 worldPosition = baseWorldPosition;
    worldPosition.xz += displacement.xz * water.simulation.y * 5.0;
    // 应用垂直位移（波高）
    worldPosition.y += displacement.y * 10.0;

    // 斜率缩放：simulation.z 控制法线扰动强度
    vec2 slope = normalAux.xy * water.simulation.z;

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
    fragNormalAux = normalAux;

    // MVP 变换到裁剪空间，输出最终顶点位置
    gl_Position =
        camera.projection *
        camera.view *
        vec4(worldPosition, 1.0);
}