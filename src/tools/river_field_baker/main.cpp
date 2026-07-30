// ===== River field baker (GUI) entry point =====
// Launches a window + ImGui panel to edit control points / spline / field
// config / shore params, then bake and save the .bin bundle the runtime reads.

#include "tools/river_field_baker/RiverFieldBakerApp.h"

#include <exception>
#include <iostream>

int main()
{
    try{
        RiverFieldBakerApp app;
        app.Run();
    }
    catch(const std::exception& e){
        std::cerr << e.what() << "\n";
        return 1;
    }

    return 0;
}
