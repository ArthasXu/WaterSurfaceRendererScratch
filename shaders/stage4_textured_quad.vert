#version 450

layout(location = 0) in vec3 inPosition; // 位置
layout(location = 1) in vec3 inColor; // 颜色
layout(location = 2) in vec2 inUV; // 纹理坐标

layout(set = 0, binding = 0) uniform VertexUBO {
    mat4 model; // 物体自身变换 可以让四边形独立旋转、平移、缩放，不依赖修改顶点缓冲区
    mat4 view; // 相机视图变换 将世界空间坐标转换到相机视角，让四边形随相机移动/旋转，实现第一人称或漫游效果
    mat4 proj; // 透视投影 产生近大远小的透视效果，并可适配窗口宽高比
} ubo; // 顶点属性，这三者组合成 MVP 矩阵，使得每个顶点的局部坐标最终被正确映射到屏幕像素
// 将固定位置的物体转变为可动态移动、缩放、旋转的 3D 物体，并适配任意相机视角和透视投影
// CPU 每帧可以更新 UBO 中的矩阵，从而让四边形和相机在每一帧都呈现不同的状态

layout(location = 0) out vec3 fragColor; // 颜色
layout(location = 1) out vec2 fragUV; // 纹理坐标

void main()
{
    // gl_Position 是一个内建的输出变量，用于告诉 GPU 光栅化器这个顶点最终在裁剪空间（Clip Space）中的位置
    gl_Position = ubo.proj * ubo.view * ubo.model * vec4(inPosition, 1.0);
    fragColor = inColor; // 颜色
    fragUV = inUV; // 纹理坐标
}