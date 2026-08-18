#version 450

layout(location = 0) in vec4 vParam;
layout(location = 1) in vec4 vWorldAlpha;
layout(location = 2) in vec4 vParam2;
layout(location = 3) in vec4 vParam3;
layout(location = 4) in vec4 vParam4;

layout(location = 0) out vec4 outColor;

float Hash21(vec2 p)
{
    p = fract(p * vec2(123.34, 456.21));
    p += dot(p, p + 45.32);
    return fract(p.x * p.y);
}

float ValueNoise(vec2 p)
{
    vec2 i = floor(p);
    vec2 f = fract(p);
    vec2 u = f * f * (3.0 - 2.0 * f);

    float a = Hash21(i);
    float b = Hash21(i + vec2(1.0, 0.0));
    float c = Hash21(i + vec2(0.0, 1.0));
    float d = Hash21(i + vec2(1.0, 1.0));

    return mix(mix(a, b, u.x), mix(c, d, u.x), u.y);
}

float FBM2(vec2 p)
{
    float sum = 0.0;
    float amp = 0.5;
    float norm = 0.0;

    for(int i = 0; i < 3; ++i){
        sum += amp * ValueNoise(p);
        norm += amp;
        p *= 2.03;
        amp *= 0.5;
    }

    return sum / norm;
}

void main()
{
    float lateral = vParam.x;
    float signedDistance = vParam.z;
    float seed = vParam.w;

    float edgeJitterMeters = vParam2.x;
    float time = vParam3.x;
    float hardCrestWidth = max(vParam4.x, 1.0);

    float edgeFade =
        1.0 - smoothstep(0.82, 1.0, abs(lateral));

    float frontMacro =
        (FBM2(vec2(lateral * 2.0 + seed * 17.3,
                   time * 0.08 + seed * 9.1)) - 0.5) *
        edgeJitterMeters;

    float jitteredSd = signedDistance + frontMacro;
    float absSd = abs(jitteredSd);

    float hardCore =
        1.0 - smoothstep(hardCrestWidth * 0.55, hardCrestWidth * 1.15, absSd);

    float softEdge =
        1.0 - smoothstep(hardCrestWidth * 1.10, hardCrestWidth * 3.50, absSd);

    float coverage = clamp(max(hardCore, softEdge * 0.50), 0.0, 1.0);

    float alpha =
        vWorldAlpha.w * coverage * edgeFade;

    if(alpha < 0.004){
        discard;
    }

    vec3 foamWet = vec3(0.76, 0.74, 0.68);
    vec3 foamWhite = vec3(0.95, 0.94, 0.90);
    vec3 foamHighlight = vec3(0.99, 0.98, 0.94);

    vec3 color = mix(foamWet, foamWhite, clamp(hardCore + softEdge * 0.25, 0.0, 1.0));
    color = mix(color, foamHighlight, hardCore * 0.25);

    outColor = vec4(color, alpha);
}
