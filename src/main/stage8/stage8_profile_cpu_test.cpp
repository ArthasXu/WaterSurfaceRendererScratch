#include "scene/water/bore/BoreWaveProfile.h"
#include "scene/water/debug/BoreProfileWriter.h"

#include <cmath>
#include <exception>
#include <iostream>
#include <stdexcept>
#include <filesystem> 

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

        water::WriteBoreProfileDebugPGMs(
            "output/stage8",
            animatedProfile
        );

        return 0;
    }
    catch(const std::exception& e){
        std::cerr << e.what() << "\n";
        return 1;
    }
}