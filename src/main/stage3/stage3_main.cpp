#include "main/stage3/ClearColorApp.h"
#include "scene/Camera.h"

#include <exception>
#include <iostream>

int main()
{
    try{
        scene::Camera camera;

        auto view0 = camera.GetViewMatrix(); // 获取初始视图矩阵
        auto proj = camera.GetProjectionMatrix(1280.0f / 720.0f); // 获取投影矩阵

        camera.MoveForward(1.0f); // 向前移动1个单位
        auto view1 = camera.GetViewMatrix(); // 获取移动后的视图矩阵

        camera.AddYawPitch(30.0f, -10.0f); // 鼠标移动

        std::cout << "Position: "
            << camera.GetPosition().x << ", "
            << camera.GetPosition().y << ", "
            << camera.GetPosition().z << "\n";

        std::cout << "Forward: "
            << camera.GetForward().x << ", "
            << camera.GetForward().y << ", "
            << camera.GetForward().z << "\n";

        ClearColorApp app; // 创建应用程序对象
        app.Run(); // 运行应用程序
        return 0;
    }
    catch(const std::exception& e){
        std::cerr << e.what() << "\n";
        return 1;
    }
}