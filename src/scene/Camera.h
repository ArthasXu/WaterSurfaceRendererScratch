#pragma once

#define GLM_FORCE_DEPTH_ZERO_TO_ONE // 深度从0到1
#include <glm/glm.hpp>

namespace scene
{
class Camera
{
public:
    Camera();

    void UpdateVectors();

    void MoveForward(float distance); // 向前移动
    void MoveRight(float distance); // 向右移动
    void MoveUp(float distance); // 向上移动

    void AddYawPitch(float yawOffset, float pitchOffset); // 鼠标移动

    glm::mat4 GetViewMatrix() const; // 获取视图矩阵
    glm::mat4 GetProjectionMatrix(float aspectRatio) const; // 获取投影矩阵

    const glm::vec3& GetPosition() const;
    const glm::vec3& GetForward() const;
    const glm::vec3& GetRight() const;
    const glm::vec3& GetUp() const;

    float GetYaw() const; // 获取偏航角
    float GetPitch() const; // 获取俯仰角

private:
    glm::vec3 m_Position = {0.0f, 0.0f, 3.0f}; // 位置

    float m_Yaw = -90.0f; // 偏航角
    float m_Pitch = 0.0f; // 俯仰角
    float m_Fov = 45.0f; // 视野

    float m_MoveSpeed = 5.0f;
    float m_MouseSensitivity = 0.1f;

    glm::vec3 m_Forward = {0.0f, 0.0f, -1.0f};
    glm::vec3 m_Right = {1.0f, 0.0f, 0.0f};
    glm::vec3 m_Up = {0.0f, 1.0f, 0.0f};
};
}