#version 450

layout(location = 0) in vec4 vParam;
layout(location = 1) in vec4 vWorldAlpha;

layout(location = 0) out vec4 outColor;

layout(set = 1, binding = 1) uniform sampler2D foamDetailTexture;

float SoftUnion(float a, float b)
{
    return 1.0 - (1.0 - a) * (1.0 - b);
}

void main()
{
    float lateral = vParam.x;
    float depth01 = vParam.y;
    float signedDistance = vParam.z;
    float seed = vParam.w;

    vec2 worldXZ = vWorldAlpha.xz;

    float crestCore =
        exp(-(signedDistance * signedDistance) / (2.0 * 10.0 * 10.0));

    float behind =
        max(-signedDistance, 0.0);

    float wakeEnvelope =
        smoothstep(0.0, 18.0, behind) *
        exp(-behind / 150.0);

    float macroNoise =
        texture(
            foamDetailTexture,
            vec2(lateral * 0.35 + seed * 7.1,
                 behind * 0.012 + seed * 3.7)
        ).r;

    float breakup =
        texture(
            foamDetailTexture,
            worldXZ * 0.018 + vec2(seed * 11.3, seed * 5.7)
        ).a;

    float wakeFoam =
        wakeEnvelope *
        smoothstep(0.18, 0.72, macroNoise) *
        mix(0.55, 1.0, breakup);

    float coverage =
        SoftUnion(crestCore, wakeFoam);

    float edgeFade =
        1.0 - smoothstep(0.82, 1.0, abs(lateral));

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

    vec3 color =
        mix(
            foamWarm,
            foamWhite,
            clamp(crestCore * 1.4, 0.0, 1.0)
        );

    outColor =
        vec4(color, alpha);
}