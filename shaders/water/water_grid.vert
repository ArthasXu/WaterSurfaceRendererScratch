#version 450

layout(location = 0) in vec3 inPosition; // 位置, 模型局部坐标
layout(location = 1) in vec3 inNormal; // 法线
layout(location = 2) in vec2 inUV; // 纹理坐标

layout(set = 0, binding = 0) uniform CameraUBO {
    mat4 model;         // 物体自身变换 平移、旋转、缩放
    mat4 view;          // 相机视图变换
    mat4 projection;    // 透视投影
    ivec4 debug;        // 调试信息
} camera; // UBO

layout(location = 0) out vec3 worldPosition; // 世界空间坐标
layout(location = 1) out vec3 worldNormal; // 世界空间法线
layout(location = 2) out vec2 uv; // 世界空间纹理坐标

void main(){
    vec4 world = camera.model * vec4(inPosition, 1.0); // 世界空间坐标

    worldPosition = world.xyz;
    worldNormal = normalize(mat3(camera.model) * inNormal); // 世界空间法线
    uv = inUV;

    gl_Position = camera.projection * camera.view * world; // 相机空间
    // eg. 一个 inPosition = (2,0,-3) 的局部点，model平移最终被摆放到世界 (7,0,-3)
    // 再经相机观察和投影，变成屏幕上一个具体的像素位置
    // 你后续的片段着色器就能利用 worldPosition、worldNormal、uv 进行复杂的着色运算
}