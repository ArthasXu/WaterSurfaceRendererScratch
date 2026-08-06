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

    float behind =
        max(-signedDistance, 0.0);

    // 片元级前沿抖动：随时间缓慢流动，只影响 alpha，不改几何
    float frontMacro =
        (FBM2(vec2(lateral * 2.0 + seed * 17.3,
                   time * 0.08 + seed * 9.1)) - 0.5) *
        edgeJitterMeters;

    float frontDetail =
        (FBM2(vec2(lateral * 7.5 + seed * 31.7,
                   behind * 0.020 - time * 0.18 + seed * 5.4)) - 0.5) *
        edgeJitterMeters * 0.35;

    float jitteredSd =
        signedDistance + frontMacro + frontDetail;

    // 主潮脊：frontWidth 可控
    float crestCore =
        exp(-(jitteredSd * jitteredSd) /
            (2.0 * hardCrestWidth * hardCrestWidth));

    // 可见浮沫区间：wakeStart ~ wakeEnd，wakeFeather 控制两端软边
    float wakeEnvelope =
        smoothstep(wakeStart, wakeStart + wakeFeather, behind) *
        (1.0 - smoothstep(wakeEnd, wakeEnd + wakeFeather, behind));

    float wakeT =
        clamp((behind - wakeStart) / max(wakeEnd - wakeStart, 1.0), 0.0, 1.0);

    // 大块浮沫团：两个不同尺度，随时间缓慢向后平流
    float patchA =
        FBM2(vec2(
            lateral * 2.0 + seed * 13.1,
            wakeT * 4.0 - time * 0.16 + seed * 7.7
        ));

    float patchB =
        FBM2(vec2(
            lateral * 5.0 + seed * 19.4,
            wakeT * 9.0 - time * 0.28 + seed * 2.3
        ));

    float macroPatch =
        patchA * 0.70 + patchB * 0.30;

    // 大孔洞：用于把连续白带切成大片浮沫
    float holeNoise =
        FBM2(vec2(
            lateral * 10.0 + seed * 23.3,
            wakeT * 12.0 - time * 0.20 + seed * 3.9
        ));

    float patchMask =
        smoothstep(
            wakePatchThreshold,
            wakePatchThreshold + 0.16,
            macroPatch
        );

    float holeMask =
        mix(
            1.0,
            smoothstep(0.42, 0.74, holeNoise),
            wakeHoleStrength
        );

    // 远尾稀疏残余：只留下零星条带，不形成连续透明带
    float residualStreak =
        exp(-behind / (wakeWidth * 1.25)) *
        smoothstep(
            0.68,
            0.86,
            FBM2(vec2(
                lateral * 16.0 + seed * 41.0,
                wakeT * 10.0 - time * 0.12 + seed * 2.0
            ))
        ) *
        0.18;

    float wakeFoam =
        wakeEnvelope *
        patchMask *
        holeMask *
        wakeFoamStrength;

    wakeFoam =
        SoftUnion(wakeFoam, residualStreak);

    // 近前缘保持连续，后方改为大片无规则浮沫
    float coverage =
        SoftUnion(crestCore, wakeFoam);
    float edgeFade =
        1.0 - smoothstep(0.88, 1.0, abs(lateral));

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

    // 雾感跟随泡沫团，不再形成连续透明白带
    float mistHint =
        wakeFoam *
        (1.0 - crestCore) *
        0.35;

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