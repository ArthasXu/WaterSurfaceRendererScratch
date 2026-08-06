#version 450

layout(set = 0, binding = 0) uniform CameraUBO {
    mat4 model;
    mat4 view;
    mat4 projection;
    vec4 cameraWorldPosition;
    ivec4 debug;
} camera;

layout(location = 0) in vec4 inPositionAlpha;
layout(location = 1) in vec4 inParam;

layout(location = 0) out vec4 vParam;
layout(location = 1) out vec4 vWorldAlpha;

void main()
{
    vec3 worldPos = inPositionAlpha.xyz;
    vParam = inParam;
    vWorldAlpha = vec4(worldPos, inPositionAlpha.w);

    gl_Position =
        camera.projection *
        camera.view *
        vec4(worldPos, 1.0);
}