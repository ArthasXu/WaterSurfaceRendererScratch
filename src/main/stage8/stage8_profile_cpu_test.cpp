#include "scene/water/bore/BoreWaveProfile.h"
#include "scene/water/debug/BoreProfileWriter.h"

#include <cmath>
#include <exception>
#include <iostream>
#include <stdexcept>
#include <filesystem> 
#include <algorithm>

namespace
{
void Require(bool condition, const char* message)
{
    if(!condition){
        throw std::runtime_error(message);
    }
}

bool IsFinite(float value)
{
    return std::isfinite(value);
}

float MaxAbsDisplacementRow(
    const water::BoreWaveProfileData& profile,
    uint32_t row
)
{
    float result = 0.0f;

    for(uint32_t x = 0; x < profile.width; x++){
        size_t index =
            static_cast<size_t>(row) *
            profile.width +
            x;

        const glm::vec4& value =
            profile.displacement[index];

        result =
            std::max(
                result,
                std::max(
                    std::max(std::abs(value.r), std::abs(value.g)),
                    std::max(std::abs(value.b), std::abs(value.a))
                )
            );
    }

    return result;
}

float MaxAdjacentRowDifference(
    const water::BoreWaveProfileData& profile
)
{
    float result = 0.0f;

    for(uint32_t y = 1; y < profile.height; y++){
        for(uint32_t x = 0; x < profile.width; x++){
            size_t current =
                static_cast<size_t>(y) *
                profile.width +
                x;

            size_t previous =
                static_cast<size_t>(y - 1) *
                profile.width +
                x;

            glm::vec4 displacementDelta =
                profile.displacement[current] -
                profile.displacement[previous];

            result =
                std::max(
                    result,
                    std::max(
                        std::max(std::abs(displacementDelta.r), std::abs(displacementDelta.g)),
                        std::max(std::abs(displacementDelta.b), std::abs(displacementDelta.a))
                    )
                );
        }
    }

    return result;
}
}

int main()
{
    try{
        water::BoreWaveProfileConfig config{};
        water::BoreWaveProfileData profile =
            water::GenerateStaticBoreWaveProfile(config, 0.55f);

        Require(profile.width == config.distanceResolution, "Unexpected profile width");
        Require(profile.height == 1, "Unexpected profile height");
        Require(profile.displacement.size() == profile.width, "Invalid displacement size");
        Require(profile.derivative.size() == profile.width, "Invalid derivative size");

        float maxUpward = -1.0e30f;
        float minUpward = 1.0e30f;
        float maxForward = -1.0e30f;
        float maxFoamWithoutCrest = 0.0f;
        bool hasPositiveSlope = false;
        bool hasNegativeSlope = false;

        for(uint32_t x = 0; x < profile.width; x++){
            const glm::vec4& displacement =
                profile.displacement[x];

            const glm::vec4& derivative =
                profile.derivative[x];

            Require(IsFinite(displacement.r), "forward has NaN or Inf");
            Require(IsFinite(displacement.g), "upward has NaN or Inf");
            Require(IsFinite(displacement.b), "foamSource has NaN or Inf");
            Require(IsFinite(displacement.a), "crestMask has NaN or Inf");
            Require(IsFinite(derivative.r), "dForwardDs has NaN or Inf");
            Require(IsFinite(derivative.g), "dUpwardDs has NaN or Inf");
            Require(IsFinite(derivative.b), "flow speed has NaN or Inf");
            Require(IsFinite(derivative.a), "breaking weight has NaN or Inf");

            maxUpward =
                std::max(maxUpward, displacement.g);

            minUpward =
                std::min(minUpward, displacement.g);

            maxForward =
                std::max(maxForward, displacement.r);

            if(displacement.b > 0.05f && displacement.a < 0.02f){
                maxFoamWithoutCrest =
                    std::max(maxFoamWithoutCrest, displacement.b);
            }

            if(derivative.g > 0.05f){
                hasPositiveSlope = true;
            }

            if(derivative.g < -0.05f){
                hasNegativeSlope = true;
            }
        }

        Require(maxUpward > config.crestHeight * 0.75f, "Missing main upward crest");
        Require(minUpward < -0.05f, "Missing rear trough");
        Require(maxForward > config.forwardDisplacement * 0.6f, "Missing forward displacement");
        Require(hasPositiveSlope, "Missing positive upward slope");
        Require(hasNegativeSlope, "Missing negative upward slope");
        Require(maxFoamWithoutCrest > 0.05f, "Foam source is not separated from crest mask");

        Require(std::abs(profile.displacement.front().r) < 0.1f, "Left forward edge not near zero");
        Require(std::abs(profile.displacement.back().r) < 0.1f, "Right forward edge not near zero");
        Require(std::abs(profile.displacement.front().g) < 0.1f, "Left upward edge not near zero");
        Require(std::abs(profile.displacement.back().g) < 0.1f, "Right upward edge not near zero");

        std::filesystem::create_directories("output/stage8");
        water::WriteBoreProfileCSV(
            "output/stage8/stage8_bore_profile_static.csv",
            profile
        );

        std::cout << "Stage8 bore profile CPU validation passed\n";
        std::cout << "maxUpward = " << maxUpward << "\n";
        std::cout << "minUpward = " << minUpward << "\n";
        std::cout << "maxForward = " << maxForward << "\n";
        std::cout << "maxFoamWithoutCrest = " << maxFoamWithoutCrest << "\n";

        water::BoreWaveProfileData animatedProfile =
            water::GenerateAnimatedBoreWaveProfile(config);

        Require(animatedProfile.width == config.distanceResolution, "Unexpected animated profile width");
        Require(animatedProfile.height == config.phaseResolution, "Unexpected animated profile height");

        size_t animatedPixelCount =
            static_cast<size_t>(animatedProfile.width) *
            animatedProfile.height;

        Require(
            animatedProfile.displacement.size() == animatedPixelCount,
            "Invalid animated displacement size"
        );

        Require(
            animatedProfile.derivative.size() == animatedPixelCount,
            "Invalid animated derivative size"
        );

        float maxAnimatedUpward = 0.0f;
        float maxAnimatedCrest = 0.0f;
        float maxAnimatedFoam = 0.0f;

        for(size_t i = 0; i < animatedPixelCount; i++){
            const glm::vec4& displacement =
                animatedProfile.displacement[i];

            const glm::vec4& derivative =
                animatedProfile.derivative[i];

            Require(IsFinite(displacement.r), "animated forward has NaN or Inf");
            Require(IsFinite(displacement.g), "animated upward has NaN or Inf");
            Require(IsFinite(displacement.b), "animated foamSource has NaN or Inf");
            Require(IsFinite(displacement.a), "animated crestMask has NaN or Inf");

            Require(IsFinite(derivative.r), "animated dForwardDs has NaN or Inf");
            Require(IsFinite(derivative.g), "animated dUpwardDs has NaN or Inf");
            Require(IsFinite(derivative.b), "animated flowSpeed has NaN or Inf");
            Require(IsFinite(derivative.a), "animated breakingWeight has NaN or Inf");

            Require(displacement.b >= -0.001f && displacement.b <= 1.001f, "animated foamSource out of range");
            Require(displacement.a >= -0.001f && displacement.a <= 1.001f, "animated crestMask out of range");
            Require(derivative.a >= -0.001f && derivative.a <= 1.001f, "animated breakingWeight out of range");

            maxAnimatedUpward =
                std::max(maxAnimatedUpward, displacement.g);

            maxAnimatedCrest =
                std::max(maxAnimatedCrest, displacement.a);

            maxAnimatedFoam =
                std::max(maxAnimatedFoam, displacement.b);
        }

        Require(
            MaxAbsDisplacementRow(animatedProfile, 0) < 0.05f,
            "Animated first row is not near flat"
        );

        Require(
            MaxAbsDisplacementRow(animatedProfile, animatedProfile.height - 1) < 0.05f,
            "Animated last row is not near flat"
        );

        Require(
            maxAnimatedUpward > config.crestHeight * 0.5f,
            "Animated profile missing visible crest"
        );

        Require(
            maxAnimatedCrest > 0.5f,
            "Animated profile missing crest mask"
        );

        Require(
            maxAnimatedFoam > 0.2f,
            "Animated profile missing foam source"
        );

        Require(
            MaxAdjacentRowDifference(animatedProfile) < config.crestHeight * 0.25f,
            "Animated profile has discontinuous adjacent rows"
        );

        water::WriteBoreProfileDebugPGMs(
            "output/stage8",
            animatedProfile
        );

        std::cout << "Animated profile validation passed\n";
        std::cout << "maxAnimatedUpward = " << maxAnimatedUpward << "\n";
        std::cout << "maxAnimatedCrest = " << maxAnimatedCrest << "\n";
        std::cout << "maxAnimatedFoam = " << maxAnimatedFoam << "\n";

        return 0;
    }
    catch(const std::exception& e){
        std::cerr << e.what() << "\n";
        return 1;
    }
}