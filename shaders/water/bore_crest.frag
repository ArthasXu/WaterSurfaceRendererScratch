#version 450

layout(location = 0) in vec4 vParam;
layout(location = 1) in vec4 vWorldAlpha;
layout(location = 2) in vec4 vParam2;
layout(location = 3) in vec4 vParam3;
layout(location = 4) in vec4 vParam4;

layout(location = 0) out vec4 outColor;

layout(set = 1, binding = 1) uniform sampler2D foamDetailTexture;

float SoftUnion(float a, float b)
{
    return 1.0 - (1.0 - a) * (1.0 - b);
}

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
    float depth01 = vParam.y;
    float signedDistance = vParam.z;
    float seed = vParam.w;

    vec2 worldXZ = vWorldAlpha.xz;

    float edgeJitterMeters = vParam2.x;
    float wakePatchThreshold = vParam2.y;
    float wakeFoamStrength = vParam2.z;
    float wakeHoleStrength = vParam2.w;

    float time = vParam3.x;
    float wakeWidth = max(vParam3.y, 1.0);
    float frontWidth = max(vParam3.z, 1.0);

    float hardCrestWidth = max(vParam4.x, 1.0);
    float wakeStart = max(vParam4.y, 0.0);
    float wakeEnd = max(vParam4.z, wakeStart + 1.0);
    float wakeFeather = max(vParam4.w, 1.0);

    float hardCrestWidth = max(vParam4.x, 1.0);

    float frontMacro =
        (FBM2(vec2(lateral * 2.0 + seed * 17.3,
                   time * 0.08 + seed * 9.1)) - 0.5) *
        edgeJitterMeters;

    float frontDetail =
        (FBM2(vec2(lateral * 7.5 + seed * 31.7,
                   time * 0.18 + seed * 5.4)) - 0.5) *
        edgeJitterMeters * 0.35;

    float jitteredSd =
        signedDistance + frontMacro + frontDetail;

    float crestCore =
        exp(-(jitteredSd * jitteredSd) /
            (2.0 * hardCrestWidth * hardCrestWidth));

    float softCrest =
        exp(-(jitteredSd * jitteredSd) /
            (2.0 * (hardCrestWidth * 2.4) * (hardCrestWidth * 2.4)));

    float coverage =
        max(crestCore, softCrest * 0.35);

    float alpha =
        vWorldAlpha.w *
        coverage *
        edgeFade;

    if(alpha < 0.01){
        discard;
    }

    vec3 foamWhite =
        vec3(0.96, 0.95, 0.91);

    vec3 foamWarm =
        vec3(0.82, 0.82, 0.76);

    float mistHint = 0.0;

    vec3 color =
        mix(
            foamWarm,
            foamWhite,
            clamp(crestCore * 1.4, 0.0, 1.0)
        );

    color =
        mix(
            color,
            vec3(0.80, 0.86, 0.90),
            mistHint * 0.25
        );

    alpha =
        max(alpha, mistHint * 0.12);

    outColor =
        vec4(color, alpha);
}