#include "main/stage6/Stage6GPUFFTApp.h"

#include <exception>
#include <iostream>

int main()
{
    try{
        Stage6GPUFFTApp app;
        app.Run();
    }
    catch(const std::exception& e){
        std::cerr << e.what() << "\n";
        return 1;
    }

    return 0;
}