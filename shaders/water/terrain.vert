#version 450

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;   // WaterVertex 第二属性，忽略
layout(location = 2) in vec2 inUV;

layout(set = 0, binding = 0) uniform CameraUBO {
    mat4 model;
    mat4 view;
    mat4 projection;
    vec4 cameraWorldPosition;
    ivec4 debug;
} camera;
layout(set = 0, binding = 21) uniform sampler2D shoreMaskTexture;

layout(location = 0) out vec3 fragWorldPos;
layout(location = 1) out vec4 fragShore;

void main(){
    vec4 shore = textureLod(shoreMaskTexture, inUV, 0.0);
    vec3 worldPos = vec3(inPosition.x, shore.a, inPosition.z);  // A = terrainHeight
    fragWorldPos = worldPos;
    fragShore = shore;
    gl_Position = camera.projection * camera.view * vec4(worldPos, 1.0);
}