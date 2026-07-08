#include "main/stage5/Stage5WaterGridApp.h"

#include <exception>
#include <iostream>

int main()
{
    try{
        Stage5WaterGridApp app;
        app.Run();
        return 0;
    }
    catch(const std::exception& e){
        std::cerr << e.what() << "\n";
        return 1;
    }
}