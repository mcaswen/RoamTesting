#pragma once

#include <SDL.h>

#include <string>

namespace ParallelRoam::Platform
{
/// <summary>
/// 持有 SDL 窗口和 OpenGL context 的平台层封装
/// </summary>
class Window
{
public:
    Window() = default;
    ~Window();

    Window(const Window&) = delete;
    Window& operator=(const Window&) = delete;

    bool Create(const std::string& title, int width, int height);

    /// <summary>
    /// 按 OpenGL context、窗口、SDL 的顺序释放平台资源
    /// </summary>
    void Destroy();

    void ProcessEvent(const SDL_Event& event);
    void SwapBuffers() const;

    // 运行时切换 swap interval，用于在 benchmark 中解除帧率限制
    bool SetVSyncEnabled(bool enabled);

    /// <summary>
    /// 切换相对鼠标模式，按住右键观察时使用
    /// </summary>
    void SetRelativeMouseMode(bool enabled);

    void RefreshSize();

    [[nodiscard]] SDL_Window* NativeWindow() const;
    [[nodiscard]] SDL_GLContext GlContext() const;
    [[nodiscard]] int Width() const;
    [[nodiscard]] int Height() const;
    [[nodiscard]] int DrawableWidth() const;
    [[nodiscard]] int DrawableHeight() const;

    // 返回最近一次成功设置的 swap interval 状态
    [[nodiscard]] bool VSyncEnabled() const;
    [[nodiscard]] bool IsValid() const;

private:
    // SDL 资源由本类独占，外部只借用原生指针
    SDL_Window* _window{nullptr};
    SDL_GLContext _glContext{nullptr};
    int _width{0};
    int _height{0};
    int _drawableWidth{0};
    int _drawableHeight{0};
    int _swapInterval{-1};
    bool _relativeMouseMode{false};
};
} // 命名空间 ParallelRoam::Platform
