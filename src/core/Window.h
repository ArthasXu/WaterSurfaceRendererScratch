#pragma once

#include <vulkan/vulkan.h>

#include <functional>
#include <string>

struct GLFWwindow;

namespace core
{
class Window
{
public:
    Window(int width, int height, const std::string& title); // 构造函数
    ~Window();  // 析构函数

    Window(const Window&) = delete; // 禁止拷贝构造函数
    Window& operator=(const Window&) = delete; // 禁止赋值运算符

    GLFWwindow* GetNativeWindow() const; // 获取原生窗口句柄
    bool ShouldClose() const; // 判断窗口是否应该关闭
    void PollEvents() const; // 轮询窗口事件
    void SetTitle(const std::string& title); // 设置窗口标题
    VkExtent2D GetFramebufferExtent() const; // 获取帧缓冲区的尺寸

    std::function<void(int, int)> OnFramebufferResize; // 帧缓冲区大小改变事件回调函数
    std::function<void(double, double)> OnMouseMove; // 鼠标移动事件回调函数
    std::function<void(int, int, int)> OnMouseButton; // 鼠标按钮事件回调函数
    std::function<void(int, int, int, int)> OnKey; // 键盘事件回调函数

private:
    void createWindow(); // 创建窗口

private:
    GLFWwindow* m_Window = nullptr; // 原生窗口句柄
    int m_Width = 1280; // 窗口宽度
    int m_Height = 720; // 窗口高度
    std::string m_Title = "Vulkan Window"; // 窗口标题
};
}