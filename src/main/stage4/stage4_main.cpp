#include "scene/TexturedQuadApp.h"

#include <exception>
#include <iostream>

int main()
{
    try {
        TexturedQuadApp app;
        app.Run();
        return 0;
    }
    catch (const std::exception& e) {
        std::cerr << e.what() << "\n";
        return 1;
    }
}
