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

    // 默认：显示 UV 坐标（红绿通道）
    outColor = vec4(fragUV, 0.0, 1.0);
}