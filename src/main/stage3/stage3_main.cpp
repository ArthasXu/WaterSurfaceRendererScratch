#include "main/stage3/ClearColorApp.h"

#include <exception>
#include <iostream>

int main()
{
    try{
        ClearColorApp app; // 创建应用程序对象
        app.Run(); // 运行应用程序
        return 0;
    }
    catch(const std::exception& e){
        std::cerr << e.what() << "\n";
        return 1;
    }
}