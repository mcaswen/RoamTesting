#pragma once

#include <SDL.h>

#include <cstdint>
#include <string>

namespace ParallelRoam::Platform
{
/// <summary>
/// 只持有 SDL 生命周期和原生窗口，不拥有任何图形 API 上下文。
/// </summary>
class Window
{
public:
    Window() = default;
    ~Window();

    Window(const Window&) = delete;
    Window& operator=(const Window&) = delete;

    bool Initialize();
    bool Create(const std::string& title, int width, int height, std::uint32_t windowFlags);

    /// <summary>
    /// 按窗口、SDL 的顺序释放平台资源；图形后端必须先自行 Shutdown。
    /// </summary>
    void Destroy();

    void ProcessEvent(const SDL_Event& event);
    /// <summary>
    /// 切换相对鼠标模式，按住右键观察时使用
    /// </summary>
    void SetRelativeMouseMode(bool enabled);

    void RefreshSize();

    [[nodiscard]] SDL_Window* NativeWindow() const;
    [[nodiscard]] int Width() const;
    [[nodiscard]] int Height() const;
    [[nodiscard]] bool IsValid() const;

private:
    // SDL 资源由本类独占，图形后端只借用原生窗口指针
    SDL_Window* _window{nullptr};
    int _width{0};
    int _height{0};
    bool _relativeMouseMode{false};
    bool _sdlInitialized{false};
};
} // 命名空间 ParallelRoam::Platform
