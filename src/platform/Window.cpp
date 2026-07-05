#include "platform/Window.h"

#include <iostream>

namespace ParallelRoam::Platform
{
Window::~Window()
{
    Destroy();
}

bool Window::Create(const std::string& title, int width, int height)
{
    // SDL main ready 在部分平台会影响 SDL 初始化入口行为
    SDL_SetMainReady();

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER | SDL_INIT_EVENTS) != 0)
    {
        std::cerr << "SDL_Init failed: " << SDL_GetError() << '\n';
        return false;
    }

    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);

#if defined(__APPLE__)
    // macOS 只支持 OpenGL 4.1 core profile，且需要 forward compatible 标志
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 4);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 1);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_FORWARD_COMPATIBLE_FLAG);
#else
    // 非 macOS 平台优先请求 4.3，后续 GPU compute 阶段会依赖更高版本能力
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 4);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, 0);
#endif

    _window = SDL_CreateWindow(
        title.c_str(),
        SDL_WINDOWPOS_CENTERED,
        SDL_WINDOWPOS_CENTERED,
        width,
        height,
        SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);

    if (_window == nullptr)
    {
        std::cerr << "SDL_CreateWindow failed: " << SDL_GetError() << '\n';
        Destroy();
        return false;
    }

    _glContext = SDL_GL_CreateContext(_window);
    if (_glContext == nullptr)
    {
        std::cerr << "SDL_GL_CreateContext failed: " << SDL_GetError() << '\n';
        Destroy();
        return false;
    }

    // OpenGL context 创建后必须绑定到当前线程，GLAD 才能加载函数指针
    if (SDL_GL_MakeCurrent(_window, _glContext) != 0)
    {
        std::cerr << "SDL_GL_MakeCurrent failed: " << SDL_GetError() << '\n';
        Destroy();
        return false;
    }

    // 阶段 0 默认开启 vsync，避免空场景渲染占满 CPU/GPU
    if (SDL_GL_SetSwapInterval(1) != 0)
    {
        std::cerr << "SDL_GL_SetSwapInterval failed: " << SDL_GetError() << '\n';
    }

    RefreshSize();
    return true;
}

void Window::Destroy()
{
    // 先退出相对鼠标模式，避免窗口销毁后系统光标仍被捕获
    SetRelativeMouseMode(false);

    // OpenGL context 必须先于 SDL window 销毁
    if (_glContext != nullptr)
    {
        SDL_GL_DeleteContext(_glContext);
        _glContext = nullptr;
    }

    if (_window != nullptr)
    {
        SDL_DestroyWindow(_window);
        _window = nullptr;
    }

    SDL_Quit();
}

void Window::ProcessEvent(const SDL_Event& event)
{
    if (event.type == SDL_WINDOWEVENT &&
        (event.window.event == SDL_WINDOWEVENT_RESIZED || event.window.event == SDL_WINDOWEVENT_SIZE_CHANGED))
    {
        RefreshSize();
    }
}

void Window::SwapBuffers() const
{
    SDL_GL_SwapWindow(_window);
}

void Window::SetRelativeMouseMode(bool enabled)
{
    // 避免每帧重复调用 SDL_SetRelativeMouseMode 导致平台层抖动
    if (_relativeMouseMode == enabled)
    {
        return;
    }

    if (SDL_SetRelativeMouseMode(enabled ? SDL_TRUE : SDL_FALSE) == 0)
    {
        _relativeMouseMode = enabled;
    }
    else
    {
        std::cerr << "SDL_SetRelativeMouseMode failed: " << SDL_GetError() << '\n';
    }
}

void Window::RefreshSize()
{
    // HiDPI 屏幕上窗口尺寸和 drawable 尺寸不同，渲染必须使用 drawable
    if (_window == nullptr)
    {
        return;
    }

    SDL_GetWindowSize(_window, &_width, &_height);
    SDL_GL_GetDrawableSize(_window, &_drawableWidth, &_drawableHeight);
}

SDL_Window* Window::NativeWindow() const
{
    return _window;
}

SDL_GLContext Window::GlContext() const
{
    return _glContext;
}

int Window::Width() const
{
    return _width;
}

int Window::Height() const
{
    return _height;
}

int Window::DrawableWidth() const
{
    return _drawableWidth;
}

int Window::DrawableHeight() const
{
    return _drawableHeight;
}

bool Window::IsValid() const
{
    return _window != nullptr && _glContext != nullptr;
}
} // 命名空间 ParallelRoam::Platform
