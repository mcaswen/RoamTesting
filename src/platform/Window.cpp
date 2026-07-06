#include "platform/Window.h"

#include <iostream>

namespace ParallelRoam::Platform
{
// Window 是唯一拥有 SDL_Window 和 SDL_GLContext 的对象
// 其他模块只能借用 native pointer
// 这样 Shutdown 顺序能集中控制
Window::~Window()
{
    Destroy();
}

bool Window::Create(const std::string& title, int width, int height)
{
    // SDL main ready 在部分平台会影响 SDL 初始化入口行为
    SDL_SetMainReady();

    // SDL_Init 成功后如果后续任一步失败
    // 都通过 Destroy 回收已创建的部分资源
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER | SDL_INIT_EVENTS) != 0)
    {
        std::cerr << "SDL_Init failed: " << SDL_GetError() << '\n';
        return false;
    }

    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    // 深度和 stencil 直接启用，后续 renderer 可共享窗口配置
    // 后续 debug overlay 和 terrain wireframe 不需要重新创建 context
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);

#if defined(__APPLE__)
    // macOS 只支持 OpenGL 4.1 core profile，且需要 forward compatible 标志
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 4);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 1);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_FORWARD_COMPATIBLE_FLAG);
#else
    // 非 macOS 平台优先请求 4.3，GPU compute 路径依赖更高版本能力
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
        // window 创建失败后也调用 Destroy
        // 它会安全处理空 context 和空 window
        std::cerr << "SDL_CreateWindow failed: " << SDL_GetError() << '\n';
        Destroy();
        return false;
    }

    _glContext = SDL_GL_CreateContext(_window);
    if (_glContext == nullptr)
    {
        // context 失败时窗口已经存在
        // Destroy 会按正确顺序释放
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

    // 默认关闭 vsync，benchmark 的 Frame ms 才能反映真实吞吐
    SetVSyncEnabled(false);

    RefreshSize();
    return true;
}

void Window::Destroy()
{
    // 先退出相对鼠标模式，避免窗口销毁后系统光标仍被捕获
    SetRelativeMouseMode(false);

    // OpenGL context 必须先于 SDL window 销毁
    // 否则某些平台会在窗口释放后访问已失效 native handle
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
    // RESIZED 和 SIZE_CHANGED 在不同平台触发时机不同
    // 两者统一刷新 cached 尺寸
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

bool Window::SetVSyncEnabled(bool enabled)
{
    const int requestedInterval = enabled ? 1 : 0;
    if (_swapInterval == requestedInterval)
    {
        return true;
    }

    if (SDL_GL_SetSwapInterval(requestedInterval) != 0)
    {
        std::cerr << "SDL_GL_SetSwapInterval failed: " << SDL_GetError() << '\n';
        return false;
    }

    _swapInterval = requestedInterval;
    return true;
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
        // 只有 SDL 调用成功后才更新缓存
        // 防止缓存状态和系统状态分叉
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

    // Window size 给 GUI 和输入层使用
    // Drawable size 给 OpenGL viewport 使用
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

bool Window::VSyncEnabled() const
{
    return _swapInterval > 0;
}

bool Window::IsValid() const
{
    return _window != nullptr && _glContext != nullptr;
}
} // 命名空间 ParallelRoam::Platform
