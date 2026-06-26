#include "core/Window.h"

#include <GLFW/glfw3.h>

#include <stdexcept>

namespace core
{
Window::Window(int width, int height, const std::string& title)
    : m_Width(width), m_Height(height), m_Title(title)
{
    createWindow(); // 创建窗口
}

Window::~Window(){
    if(m_Window != nullptr){
        glfwDestroyWindow(m_Window); // 销毁窗口
    }
}

GLFWwindow* Window::GetNativeWindow() const{
    return m_Window; // 获取原生窗口句柄
}

bool Window::ShouldClose() const{
    return glfwWindowShouldClose(m_Window); // 判断窗口是否应该关闭
}

void Window::PollEvents() const{
    glfwPollEvents(); // 轮询窗口事件
}

void Window::SetTitle(const std::string& title)
{
    m_Title = title;
    glfwSetWindowTitle(m_Window, m_Title.c_str());
}

VkExtent2D Window::GetFramebufferExtent() const{ // 获取帧缓冲区的尺寸
    int width = 0, height = 0;
    glfwGetFramebufferSize(m_Window, &width, &height); // 获取窗口宽度和高度
    return {static_cast<uint32_t>(width), static_cast<uint32_t>(height)}; // 返回帧缓冲区的尺寸    
}

void Window::createWindow(){ // 创建窗口
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API); // 不使用 OpenGL

    // GLFWwindow *glfwCreateWindow(int width, int height, const char *title, GLFWmonitor *monitor, GLFWwindow *share)
    m_Window = glfwCreateWindow(
        m_Width,            // 窗口宽度 
        m_Height,           // 窗口高度
        m_Title.c_str(),    // 窗口标题
        nullptr,            // 全屏模式
        nullptr             // 共享窗口
    ); // 创建窗口

    if(m_Window == nullptr){
        throw std::runtime_error("Failed to create GLFW window"); // 创建窗口失败
    }

    glfwSetWindowUserPointer(m_Window, this); // 设置窗口用户指针

    glfwSetFramebufferSizeCallback(
        m_Window, 
        [](GLFWwindow* window, int width, int height){
            auto* app = static_cast<Window*>(glfwGetWindowUserPointer(window)); // 获取窗口用户指针
            if(app != nullptr && app->OnFramebufferResize){
                app->OnFramebufferResize(width, height); // 调用窗口大小改变回调函数
            }
        }
    ); // 设置窗口大小回调函数

    glfwSetCursorPosCallback(
        m_Window,
        [](GLFWwindow* window, double xpos, double ypos){
            auto* app = static_cast<Window*>(glfwGetWindowUserPointer(window)); // 获取窗口用户指针
            if(app != nullptr && app->OnMouseMove){
                app->OnMouseMove(xpos, ypos); // 调用鼠标移动回调函数
            }
        }
    ); // 设置鼠标移动回调函数

    glfwSetMouseButtonCallback(
        m_Window,
        [](GLFWwindow* window, int button, int action, int mods){
            auto* app = static_cast<Window*>(glfwGetWindowUserPointer(window)); // 获取窗口用户指针
            if(app != nullptr && app->OnMouseButton){
                app->OnMouseButton(button, action, mods); // 调用鼠标按钮回调函数
            }
        }
    ); // 设置鼠标按钮回调函数

    glfwSetKeyCallback(
        m_Window,
        [](GLFWwindow* window, int key, int scancode, int action, int mods){
            auto* app = static_cast<Window*>(glfwGetWindowUserPointer(window)); // 获取窗口用户指针
            if(app != nullptr && app->OnKey){
                app->OnKey(key, scancode, action, mods); // 调用键盘回调函数
            }
        }
    ); // 设置键盘回调函数
}


}