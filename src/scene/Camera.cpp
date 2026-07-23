#include "scene/Camera.h"

#include <glm/gtc/matrix_transform.hpp>

namespace scene
{
Camera::Camera(){ // 构造函数
    UpdateVectors(); // 更新向量
}

void Camera::UpdateVectors(){ // 更新向量
    glm::vec3 forward{};
    forward.x = glm::cos(glm::radians(m_Yaw)) * glm::cos(glm::radians(m_Pitch)); // 计算前方向量
    forward.y = glm::sin(glm::radians(m_Pitch)); // 计算俯仰角
    forward.z = glm::sin(glm::radians(m_Yaw)) * glm::cos(glm::radians(m_Pitch)); // 计算前方向量

    m_Forward = glm::normalize(forward); // 归一化前方向量

    const glm::vec3 worldUp = {0.0f, 1.0f, 0.0f}; // 世界向上向量

    m_Right = glm::normalize(glm::cross(m_Forward, worldUp)); // 计算右方向量
    m_Up = glm::normalize(glm::cross(m_Right, m_Forward)); // 计算上方向量

    m_Target =
        m_Position +
        m_Forward *
        m_LookDistance;
}

void Camera::MoveForward(float distance)
{
    glm::vec3 delta = m_Forward * distance;
    m_Position += delta;
    m_Target += delta;
}

void Camera::MoveRight(float distance)
{
    glm::vec3 delta = m_Right * distance;
    m_Position += delta;
    m_Target += delta;
}

void Camera::MoveUp(float distance)
{
    glm::vec3 delta = m_Up * distance;
    m_Position += delta;
    m_Target += delta;
}

void Camera::SetPosition(const glm::vec3& position)
{
    m_Position = position;
}

void Camera::LookAt(const glm::vec3& target)
{
    m_Target = target;

    glm::vec3 direction =
        glm::normalize(target - m_Position);

    m_LookDistance =
        glm::length(target - m_Position);

    m_Pitch =
        glm::degrees(glm::asin(direction.y));

    m_Yaw =
        glm::degrees(glm::atan(direction.z, direction.x));

    UpdateVectors();
}

void Camera::AddYawPitch(float yawOffset, float pitchOffset){ // 鼠标移动
    m_Yaw += yawOffset * m_MouseSensitivity;
    m_Pitch += pitchOffset * m_MouseSensitivity;

    if(m_Pitch > 89.0f){
        m_Pitch = 89.0f;
    }

    if(m_Pitch < -89.0f){
        m_Pitch = -89.0f;
    }

    UpdateVectors();
}

glm::mat4 Camera::GetViewMatrix() const { // 获取视图矩阵
    return glm::lookAt(m_Position, m_Target, m_Up); // 获取视图矩阵
}

glm::mat4 Camera::GetProjectionMatrix(float aspectRatio) const { // 获取投影矩阵
    glm::mat4 proj = glm::perspective(glm::radians(m_Fov), aspectRatio, 0.1f, 1000.0f); // 获取投影矩阵
    proj[1][1] *= -1.0f; // 翻转Y轴
    return proj; // 返回投影矩阵
}

const glm::vec3& Camera::GetPosition() const {
    return m_Position;
}

const glm::vec3& Camera::GetForward() const {
    return m_Forward;
}

const glm::vec3& Camera::GetRight() const {
    return m_Right;
}

const glm::vec3& Camera::GetUp() const {
    return m_Up;
}

float Camera::GetYaw() const {
    return m_Yaw;
}

float Camera::GetPitch() const {
    return m_Pitch;
}


}