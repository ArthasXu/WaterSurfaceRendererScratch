#include "scene/water/bore/BoreFrontField.h"
#include "scene/water/bore/FrontParameterLUT.h"

#include <glm/glm.hpp>

#include <cmath>
#include <exception>
#include <iostream>
#include <stdexcept>

// 对 BoreFrontField 和 FrontParameterLUT 进行数值正确性测试，
// 确保基础几何计算、波前推进、LUT 导数精度等完全正确
namespace
{
void ExpectNear(float value, float expected, float epsilon, const char* name)
{
    if(std::abs(value - expected) > epsilon){
        std::cerr
            << name
            << " expected "
            << expected
            << " got "
            << value
            << "\n";

        throw std::runtime_error("Bore front CPU test failed");
    }
}
}

int main()
{
    try{
        water::BoreFrontParams params{};
        params.origin = glm::vec2(0.0f);
        params.direction = glm::vec2(1.0f, 0.0f);
        params.frontLength = 1000.0f;
        params.initialOffset = 0.0f;
        params.speed = 10.0f;
        params.edgeFadeFraction = 0.03f;

        water::BoreFrontField field(params);

        water::BoreFrontSample center =
            field.EvaluateStraight(glm::vec2(0.0f, 0.0f), 0.0f);

        ExpectNear(center.signedDistance, 0.0f, 1e-5f, "center signedDistance");
        ExpectNear(center.frontU, 0.5f, 1e-5f, "center frontU");

        water::BoreFrontSample front =
            field.EvaluateStraight(glm::vec2(10.0f, 0.0f), 0.0f);

        ExpectNear(front.signedDistance, 10.0f, 1e-5f, "front signedDistance");

        water::BoreFrontSample back =
            field.EvaluateStraight(glm::vec2(-10.0f, 0.0f), 0.0f);

        ExpectNear(back.signedDistance, -10.0f, 1e-5f, "back signedDistance");

        water::BoreFrontSample top =
            field.EvaluateStraight(glm::vec2(0.0f, 500.0f), 0.0f);

        ExpectNear(top.frontU, 1.0f, 1e-5f, "top frontU");

        water::BoreFrontSample bottom =
            field.EvaluateStraight(glm::vec2(0.0f, -500.0f), 0.0f);

        ExpectNear(bottom.frontU, 0.0f, 1e-5f, "bottom frontU");

        water::BoreFrontSample afterOneSecond =
            field.EvaluateStraight(glm::vec2(0.0f, 0.0f), 1.0f);

        ExpectNear(afterOneSecond.signedDistance, -10.0f, 1e-5f, "time signedDistance");

        water::BoreFrontSample leftOutside =
            field.EvaluateStraight(glm::vec2(0.0f, -600.0f), 0.0f);

        ExpectNear(leftOutside.lengthMask, 0.0f, 1e-5f, "leftOutside mask");

        water::BoreFrontSample centerMask =
            field.EvaluateStraight(glm::vec2(0.0f, 0.0f), 0.0f);

        ExpectNear(centerMask.lengthMask, 1.0f, 1e-5f, "center mask");

        water::FrontLUTData lut =
            water::GenerateDeterministicFrontLUT(1024);

        float maxDerivativeError = 0.0f;

        for(uint32_t i = 1; i + 1 < lut.resolution; i++){
            float du =
                1.0f /
                static_cast<float>(lut.resolution - 1);

            float finiteDifference =
                (lut.parameters[i + 1].r - lut.parameters[i - 1].r) /
                (2.0f * du);

            float analytic =
                lut.derivatives[i].r;

            maxDerivativeError =
                std::max(
                    maxDerivativeError,
                    std::abs(finiteDifference - analytic)
                );
        }

        if(maxDerivativeError > 0.1f){
            throw std::runtime_error("Front LUT derivative error too high");
        }

        std::cout << "Stage 7 bore front CPU tests passed\n";
        std::cout << "max derivative error = " << maxDerivativeError << "\n";
    }
    catch(const std::exception& e){
        std::cerr << e.what() << "\n";
        return 1;
    }

    return 0;
}