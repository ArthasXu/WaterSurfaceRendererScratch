#version 450

layout(location = 0) in vec3 fragColor; // 颜色
layout(location = 1) in vec2 fragUV; // 纹理坐标

layout(set = 0, binding = 1) uniform sampler2D texSampler;

layout(location = 0) out vec4 outColor; // 颜色

void main()
{
    vec4 texColor = texture(texSampler, fragUV);
    outColor = texColor * vec4(fragColor, 1.0);
}