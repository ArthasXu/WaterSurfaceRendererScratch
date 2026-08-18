struct FoamDetail
{
    float coverage;
    vec2 normalXZ;
    float breakup;
};

float FoamPhaseWeight(float phase)
{
    float s =
        sin(
            3.14159265359 *
            phase
        );

    return s * s;
}

FoamDetail SampleFoamPhase(
    sampler2D detailTexture,
    vec2 worldXZ,
    vec2 velocity,
    float normalizedAge,
    float cycleDuration,
    float worldScale
){
    float ageSeconds =
        normalizedAge *
        cycleDuration;

    vec2 samplePosition =
        worldXZ -
        velocity *
        ageSeconds;

    vec2 uv =
        samplePosition *
        worldScale;

    vec4 sampleValue =
        texture(detailTexture, uv);

    FoamDetail result;
    result.coverage =
        sampleValue.r;

    result.normalXZ =
        sampleValue.gb *
        2.0 -
        1.0;

    result.breakup =
        sampleValue.a;

    return result;
}

// FF MF_FluidFoamShallow：水膜极薄处淡出泡沫，避免湿沙上出现硬泡沫边
float FluxFoamShallow(float height, float depth, float offset, float scale)
{
    return clamp((height + offset) * (depth * scale), 0.0, 1.0);
}

// FF MF_FluidFoam 的 Opacity_Top：泡沫越多阈值越低 → 硬核范围越大
// intensity=-0.05, width=0.2 时等价于 saturate(0.95 - 1.1875 * shallowResult)
float FluxFoamHardness(float shallowResult, float intensity, float width)
{
    float k = (intensity + 1.0) / (width - 1.0);
    return clamp(k * ((width - 1.0) + shallowResult), 0.0, 1.0);
}