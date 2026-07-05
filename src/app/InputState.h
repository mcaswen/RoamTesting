#pragma once

#include <SDL.h>

#include <array>
#include <cstddef>

namespace ParallelRoam::App
{
/// <summary>
/// 由 SDL 事件转换得到的逐帧输入快照
/// </summary>
class InputState
{
public:
    /// <summary>
    /// 开始新帧时清理只在单帧内有效的输入量
    /// </summary>
    void BeginFrame();

    /// <summary>
    /// 吸收一个 SDL 事件并更新输入快照
    /// </summary>
    /// <param name="event">SDL 事件。</param>
    void HandleEvent(const SDL_Event& event);

    /// <summary>
    /// 更新窗口逻辑尺寸，供 GUI 和调试面板显示
    /// </summary>
    /// <param name="width">窗口宽度。</param>
    /// <param name="height">窗口高度。</param>
    void SetWindowSize(int width, int height);

    /// <summary>
    /// 返回当前是否收到退出请求
    /// </summary>
    [[nodiscard]] bool IsQuitRequested() const;

    /// <summary>
    /// 查询指定键是否处于按下状态
    /// </summary>
    /// <param name="scancode">SDL 扫描码。</param>
    [[nodiscard]] bool IsKeyDown(SDL_Scancode scancode) const;

    /// <summary>
    /// 返回鼠标右键是否按下，用于启用视角控制
    /// </summary>
    [[nodiscard]] bool IsRightMouseDown() const;

    /// <summary>
    /// 返回当前帧鼠标横向移动量
    /// </summary>
    [[nodiscard]] float MouseDeltaX() const;

    /// <summary>
    /// 返回当前帧鼠标纵向移动量
    /// </summary>
    [[nodiscard]] float MouseDeltaY() const;

    /// <summary>
    /// 返回窗口逻辑宽度
    /// </summary>
    [[nodiscard]] int WindowWidth() const;

    /// <summary>
    /// 返回窗口逻辑高度
    /// </summary>
    [[nodiscard]] int WindowHeight() const;

private:
    // SDL scancode 可直接作为数组索引，避免每帧查 map
    static constexpr std::size_t KeyCount = static_cast<std::size_t>(SDL_NUM_SCANCODES);

    // 输入层只保存当前快照，不直接触发业务逻辑
    std::array<bool, KeyCount> _keys{};
    bool _quitRequested{false};
    bool _rightMouseDown{false};
    float _mouseDeltaX{0.0F};
    float _mouseDeltaY{0.0F};
    int _windowWidth{0};
    int _windowHeight{0};
};
} // 命名空间 ParallelRoam::App
