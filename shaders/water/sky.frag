#version 450
#extension GL_GOOGLE_include_directive : require
#include "common/water_optics_common.glsl"

layout(location = 0) in vec3 vDir;
layout(location = 0) out vec4 outColor;

layout(set = 1, binding = 4) uniform WaterMaterialUBO {
    vec4 shallowColor;
    vec4 deepColor;
    vec4 sedimentColor;
    vec4 opticalParams;
    vec4 lightParams;   // xyz = 太阳方向
    vec4 fogParams;
    vec4 absorptionCoeff;
    vec4 shallowParams;
} material;

void main(){
    outColor = vec4(SkyColor(normalize(vDir), material.lightParams.xyz), 1.0);
}