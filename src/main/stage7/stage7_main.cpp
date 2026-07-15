#include "main/stage7/Stage7BoreFrontApp.h"

#include <exception>
#include <iostream>

int main()
{
    try{
        Stage7BoreFrontApp app;
        app.Run();
    }
    catch(const std::exception& e){
        std::cerr << e.what() << "\n";
        return 1;
    }

    return 0;
}