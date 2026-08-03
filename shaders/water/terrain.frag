#version 450

#include "common/water_optics_common.glsl"

layout(location = 0) in vec3 fragWorldPos;
layout(location = 1) in vec4 fragShore;
layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform CameraUBO {
    mat4 model;
    mat4 view;
    mat4 projection;
    vec4 cameraWorldPosition;
    ivec4 debug;
} camera;

layout(set = 1, binding = 4) uniform WaterMaterialUBO {
    vec4 shallowColor;
    vec4 deepColor;
    vec4 sedimentColor;
    vec4 opticalParams;
    vec4 lightParams;   // xyz = 太阳方向
    vec4 fogParams;     // x=雾起点 y=雾终点
    vec4 absorptionCoeff;
    vec4 shallowParams;
} material;

void main(){
    vec3 dx = dFdx(fragWorldPos);
    vec3 dy = dFdy(fragWorldPos);
    vec3 n = normalize(cross(dx, dy));
    if(n.y < 0.0) n = -n;

    vec3 sand  = vec3(0.76, 0.70, 0.50);
    vec3 grass = vec3(0.24, 0.34, 0.18);
    vec3 albedo = mix(grass, sand, clamp(fragShore.b, 0.0, 1.0)); // B = sand

    vec3 sunDir = normalize(material.lightParams.xyz);
    float ndl = max(dot(n, sunDir), 0.0);

    // 湿沙（岸和水衔接自然）
    float wet = clamp(fragShore.g, 0.0, 1.0);
    albedo = mix(albedo, albedo * 0.45, wet);                    // 打湿变深
    vec3  R = reflect(-sunDir, n);
    float wetSpec = pow(max(dot(R, sunDir), 0.0), 48.0) * wet * 0.25;
    vec3 color = albedo * (0.35 + 0.65 * ndl) + wetSpec;

    // 与水面一致的距离雾：远处地形融进视线方向的天空
    // float dist = length(camera.cameraWorldPosition.xyz - fragWorldPos);
    // vec3  viewDirWS = normalize(fragWorldPos - camera.cameraWorldPosition.xyz);
    // vec3  fogColor  = SkyColor(viewDirWS, sunDir);
    // color = ApplyDistanceFog(color, dist, fogColor, material.fogParams.x, material.fogParams.y);

    outColor = vec4(color, 1.0);
}