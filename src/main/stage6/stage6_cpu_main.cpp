#include "main/stage6/Stage6CPUFFTApp.h"

#include <exception>
#include <iostream>

int main()
{
    try{
        Stage6CPUFFTApp app;
        app.Run();
    }
    catch(const std::exception& e){
        std::cerr << e.what() << "\n";
        return 1;
    }

    return 0;
}