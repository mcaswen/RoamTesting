#include "app/CameraController.h"

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cmath>

namespace ParallelRoam::App
{
CameraController::CameraController()
{
    UpdateBasis();
}

void CameraController::Update(const InputState& input, float deltaSeconds)
{
    // 右键按住才更新视角，避免鼠标操作 ImGui 面板时相机乱动
    if (input.IsRightMouseDown())
    {
        _yawDegrees += input.MouseDeltaX() * _mouseSensitivity;
        _pitchDegrees -= input.MouseDeltaY() * _mouseSensitivity;
        _pitchDegrees = std::clamp(_pitchDegrees, -89.0F, 89.0F);
        UpdateBasis();
    }

    glm::vec3 movement{0.0F};

    // WASD 使用相机局部前向和右向，Space/Ctrl 保持世界竖直方向
    if (input.IsKeyDown(SDL_SCANCODE_W))
    {
        movement += _front;
    }

    if (input.IsKeyDown(SDL_SCANCODE_S))
    {
        movement -= _front;
    }

    if (input.IsKeyDown(SDL_SCANCODE_A))
    {
        movement -= _right;
    }

    if (input.IsKeyDown(SDL_SCANCODE_D))
    {
        movement += _right;
    }

    if (input.IsKeyDown(SDL_SCANCODE_SPACE))
    {
        movement += _worldUp;
    }

    if (input.IsKeyDown(SDL_SCANCODE_LCTRL) || input.IsKeyDown(SDL_SCANCODE_RCTRL))
    {
        movement -= _worldUp;
    }

    // 归一化移动向量，避免斜向移动比单轴移动更快
    if (glm::dot(movement, movement) > 0.0001F)
    {
        movement = glm::normalize(movement);
    }

    // Shift 只改变移动速度，不影响鼠标灵敏度
    const bool fastMove = input.IsKeyDown(SDL_SCANCODE_LSHIFT) || input.IsKeyDown(SDL_SCANCODE_RSHIFT);
    const float speed = fastMove ? _moveSpeed * _fastMoveMultiplier : _moveSpeed;
    _position += movement * speed * deltaSeconds;
}

glm::mat4 CameraController::GetViewMatrix() const
{
    return glm::lookAt(_position, _position + _front, _up);
}

glm::vec3 CameraController::Position() const
{
    return _position;
}

float CameraController::YawDegrees() const
{
    return _yawDegrees;
}

float CameraController::PitchDegrees() const
{
    return _pitchDegrees;
}

void CameraController::UpdateBasis()
{
    // yaw/pitch 使用角度存储，计算方向时统一转成弧度
    const float yawRadians = glm::radians(_yawDegrees);
    const float pitchRadians = glm::radians(_pitchDegrees);

    glm::vec3 front{};
    front.x = std::cos(yawRadians) * std::cos(pitchRadians);
    front.y = std::sin(pitchRadians);
    front.z = std::sin(yawRadians) * std::cos(pitchRadians);

    // 通过叉积重建正交基，避免累计旋转误差污染 view matrix
    _front = glm::normalize(front);
    _right = glm::normalize(glm::cross(_front, _worldUp));
    _up = glm::normalize(glm::cross(_right, _front));
}
} // 命名空间 ParallelRoam::App
