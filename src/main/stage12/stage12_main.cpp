#include "main/stage12/Stage12FluidFluxApp.h"

#include <exception>
#include <iostream>

int main()
{
    try{
        Stage12FluidFluxApp app;
        app.Run();
    }
    catch(const std::exception& e){
        std::cerr << e.what() << "\n";
        return 1;
    }

    return 0;
}