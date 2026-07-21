#include "main/stage10/Stage10RiverWaterApp.h"

#include <exception>
#include <iostream>

int main()
{
    try{
        Stage10RiverWaterApp app;
        app.Run();
    }
    catch(const std::exception& e){
        std::cerr << e.what() << "\n";
        return 1;
    }

    return 0;
}