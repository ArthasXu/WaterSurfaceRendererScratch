#include "main/stage9/Stage9WaterApp.h"

#include <exception>
#include <iostream>

int main()
{
    try{
        Stage9WaterApp app;
        app.Run();
    }
    catch(const std::exception& e){
        std::cerr << e.what() << "\n";
        return 1;
    }

    return 0;
}