#pragma once

#include "app/InputState.h"

#include <glm/glm.hpp>

namespace ParallelRoam::App
{
/// <summary>
/// 阶段 0 渲染闭环使用的自由飞行相机控制器
/// </summary>
class CameraController
{
public:
    CameraController();

    /// <summary>
    /// 根据当前输入状态更新位置和朝向
    /// </summary>
    /// <param name="input">当前帧输入快照。</param>
    /// <param name="deltaSeconds">当前帧耗时秒数。</param>
    void Update(const InputState& input, float deltaSeconds);

    /// <summary>
    /// 返回用于渲染的 view matrix
    /// </summary>
    [[nodiscard]] glm::mat4 GetViewMatrix() const;

    /// <summary>
    /// 返回相机世界坐标
    /// </summary>
    [[nodiscard]] glm::vec3 Position() const;

    /// <summary>
    /// 返回相机 yaw 角度，单位为度
    /// </summary>
    [[nodiscard]] float YawDegrees() const;

    /// <summary>
    /// 返回相机 pitch 角度，单位为度
    /// </summary>
    [[nodiscard]] float PitchDegrees() const;

private:
    // yaw 和 pitch 改变后统一刷新前向、右向和上向基向量
    void UpdateBasis();

    // 当前位置和朝向以世界空间存储，便于后续传给算法层做视点相关 LOD
    glm::vec3 _position{0.0F, 1.8F, 6.0F};
    glm::vec3 _front{0.0F, 0.0F, -1.0F};
    glm::vec3 _right{1.0F, 0.0F, 0.0F};
    glm::vec3 _up{0.0F, 1.0F, 0.0F};
    glm::vec3 _worldUp{0.0F, 1.0F, 0.0F};
    float _yawDegrees{-90.0F};
    float _pitchDegrees{-18.0F};
    float _moveSpeed{6.0F};
    float _fastMoveMultiplier{3.0F};
    float _mouseSensitivity{0.12F};
};
} // 命名空间 ParallelRoam::App
