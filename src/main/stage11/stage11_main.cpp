#include "main/stage11/Stage11ProgressBoreApp.h"

#include <exception>
#include <iostream>

int main()
{
    try{
        Stage11ProgressBoreApp app;
        app.Run();
    }
    catch(const std::exception& e){
        std::cerr << e.what() << "\n";
        return 1;
    }

    return 0;
}