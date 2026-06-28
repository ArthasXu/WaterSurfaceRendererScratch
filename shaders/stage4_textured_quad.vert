#version 450

layout(location = 0) in vec3 inPosition; // 位置
layout(location = 1) in vec3 inColor; // 颜色
layout(location = 2) in vec2 inUV; // 纹理坐标

layout(location = 0) out vec3 fragColor; // 颜色
layout(location = 1) out vec2 fragUV; // 纹理坐标

void main()
{
    // gl_Position 是一个内建的输出变量，用于告诉 GPU 光栅化器这个顶点最终在裁剪空间（Clip Space）中的位置
    gl_Position = vec4(inPosition, 1.0); 
    fragColor = inColor; // 颜色
    fragUV = inUV; // 纹理坐标
}