#version 450

layout(location = 0) in vec3 fragWorldPos;
layout(location = 1) in vec4 fragShore;
layout(location = 0) out vec4 outColor;

void main(){
    vec3 dx = dFdx(fragWorldPos);
    vec3 dy = dFdy(fragWorldPos);
    vec3 n = normalize(cross(dx, dy));
    if(n.y < 0.0) n = -n;

    vec3 sand  = vec3(0.76, 0.70, 0.50);
    vec3 grass = vec3(0.24, 0.34, 0.18);
    vec3 albedo = mix(grass, sand, clamp(fragShore.b, 0.0, 1.0)); // B = sand

    vec3 sunDir = normalize(vec3(0.4, 0.8, 0.3));
    float ndl = max(dot(n, sunDir), 0.0);
    outColor = vec4(albedo * (0.35 + 0.65 * ndl), 1.0);
}