#include "main/stage8/Stage8BoreProfileApp.h"

#include <exception>
#include <iostream>

int main()
{
    try{
        Stage8BoreProfileApp app;
        app.Run();
    }
    catch(const std::exception& e){
        std::cerr << e.what() << "\n";
        return 1;
    }

    return 0;
}