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