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

    /// <summary>
    /// 创建可调整大小的 SDL2 OpenGL 窗口
    /// </summary>
    /// <param name="title">窗口标题。</param>
    /// <param name="width">初始窗口宽度。</param>
    /// <param name="height">初始窗口高度。</param>
    bool Create(const std::string& title, int width, int height);

    /// <summary>
    /// 按 OpenGL context、窗口、SDL 的顺序释放平台资源
    /// </summary>
    void Destroy();

    /// <summary>
    /// 处理会影响窗口状态的 SDL 事件
    /// </summary>
    /// <param name="event">SDL 事件。</param>
    void ProcessEvent(const SDL_Event& event);

    /// <summary>
    /// 交换前后缓冲区
    /// </summary>
    void SwapBuffers() const;

    /// <summary>
    /// 切换相对鼠标模式，按住右键观察时使用
    /// </summary>
    /// <param name="enabled">是否启用相对鼠标模式。</param>
    void SetRelativeMouseMode(bool enabled);

    /// <summary>
    /// 刷新窗口逻辑尺寸和 HiDPI drawable 尺寸
    /// </summary>
    void RefreshSize();

    /// <summary>
    /// 返回原生 SDL_Window 指针
    /// </summary>
    [[nodiscard]] SDL_Window* NativeWindow() const;

    /// <summary>
    /// 返回 SDL OpenGL context
    /// </summary>
    [[nodiscard]] SDL_GLContext GlContext() const;

    /// <summary>
    /// 返回窗口逻辑宽度
    /// </summary>
    [[nodiscard]] int Width() const;

    /// <summary>
    /// 返回窗口逻辑高度
    /// </summary>
    [[nodiscard]] int Height() const;

    /// <summary>
    /// 返回 OpenGL drawable 宽度
    /// </summary>
    [[nodiscard]] int DrawableWidth() const;

    /// <summary>
    /// 返回 OpenGL drawable 高度
    /// </summary>
    [[nodiscard]] int DrawableHeight() const;

    /// <summary>
    /// 返回窗口和 OpenGL context 是否都已创建
    /// </summary>
    [[nodiscard]] bool IsValid() const;

private:
    // SDL 资源由本类独占，外部只借用原生指针
    SDL_Window* _window{nullptr};
    SDL_GLContext _glContext{nullptr};
    int _width{0};
    int _height{0};
    int _drawableWidth{0};
    int _drawableHeight{0};
    bool _relativeMouseMode{false};
};
} // 命名空间 ParallelRoam::Platform
