#include "render/OpenGlGraphicsBackend.h"

#include "gui/ImGuiLayer.h"

#include <glad/gl.h>

#include <iostream>
#include <string>

namespace ParallelRoam::Render
{
namespace
{
GLADapiproc LoadOpenGlProc(const char* name)
{
    return reinterpret_cast<GLADapiproc>(SDL_GL_GetProcAddress(name));
}

const char* ImGuiGlslVersion()
{
#if defined(__APPLE__)
    return "#version 410";
#else
    return "#version 430";
#endif
}

bool SetOpenGlAttribute(SDL_GLattr attribute, int value, const char* name, std::string* errorMessage)
{
    if (SDL_GL_SetAttribute(attribute, value) == 0)
    {
        return true;
    }

    if (errorMessage != nullptr)
    {
        *errorMessage = std::string{"SDL_GL_SetAttribute failed for "} + name + ": " + SDL_GetError();
    }
    return false;
}
} // namespace

OpenGlGraphicsBackend::~OpenGlGraphicsBackend()
{
    Shutdown();
}

GraphicsApi OpenGlGraphicsBackend::Api() const
{
    return GraphicsApi::OpenGl;
}

const char* OpenGlGraphicsBackend::Name() const
{
    return "OpenGL";
}

bool OpenGlGraphicsBackend::ConfigureWindow(std::string* errorMessage)
{
    if (!SetOpenGlAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE, "profile", errorMessage) ||
        !SetOpenGlAttribute(SDL_GL_DOUBLEBUFFER, 1, "double buffer", errorMessage) ||
        !SetOpenGlAttribute(SDL_GL_DEPTH_SIZE, 24, "depth size", errorMessage) ||
        !SetOpenGlAttribute(SDL_GL_STENCIL_SIZE, 8, "stencil size", errorMessage))
    {
        return false;
    }

#if defined(__APPLE__)
    return SetOpenGlAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 4, "major version", errorMessage) &&
           SetOpenGlAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 1, "minor version", errorMessage) &&
           SetOpenGlAttribute(
               SDL_GL_CONTEXT_FLAGS,
               SDL_GL_CONTEXT_FORWARD_COMPATIBLE_FLAG,
               "context flags",
               errorMessage);
#else
    return SetOpenGlAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 4, "major version", errorMessage) &&
           SetOpenGlAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3, "minor version", errorMessage) &&
           SetOpenGlAttribute(SDL_GL_CONTEXT_FLAGS, 0, "context flags", errorMessage);
#endif
}

std::uint32_t OpenGlGraphicsBackend::RequiredSdlWindowFlags() const
{
    return SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI;
}

bool OpenGlGraphicsBackend::Initialize(SDL_Window* window, std::string* errorMessage)
{
    if (window == nullptr)
    {
        if (errorMessage != nullptr)
        {
            *errorMessage = "OpenGL backend requires a valid SDL window";
        }
        return false;
    }

    _window = window;
    _context = SDL_GL_CreateContext(_window);
    if (_context == nullptr)
    {
        if (errorMessage != nullptr)
        {
            *errorMessage = std::string{"SDL_GL_CreateContext failed: "} + SDL_GetError();
        }
        Shutdown();
        return false;
    }

    if (SDL_GL_MakeCurrent(_window, _context) != 0)
    {
        if (errorMessage != nullptr)
        {
            *errorMessage = std::string{"SDL_GL_MakeCurrent failed: "} + SDL_GetError();
        }
        Shutdown();
        return false;
    }

    const int version = gladLoadGL(LoadOpenGlProc);
    if (version == 0)
    {
        if (errorMessage != nullptr)
        {
            *errorMessage = "gladLoadGL failed";
        }
        Shutdown();
        return false;
    }

    std::cout << "Graphics backend: OpenGL\n";
    std::cout << "OpenGL loaded: " << GLAD_VERSION_MAJOR(version) << '.' << GLAD_VERSION_MINOR(version) << '\n';
    std::cout << "OpenGL renderer: " << glGetString(GL_RENDERER) << '\n';
    std::cout << "OpenGL version: " << glGetString(GL_VERSION) << '\n';

    const auto* renderer = reinterpret_cast<const char*>(glGetString(GL_RENDERER));
    const auto* versionString = reinterpret_cast<const char*>(glGetString(GL_VERSION));
    _adapterName = renderer != nullptr ? renderer : "Unknown OpenGL adapter";
    _versionString = versionString != nullptr ? versionString : "Unknown OpenGL version";

    if (!SetVSyncEnabled(false))
    {
        std::cerr << "OpenGL backend could not disable VSync during initialization.\n";
    }
    RefreshDrawableSize();
    return true;
}

bool OpenGlGraphicsBackend::InitializeImGui(Gui::ImGuiLayer& guiLayer, std::string* errorMessage)
{
    const Gui::ImGuiOpenGlBackendConfig config{
        .Window = _window,
        .Context = _context,
        .GlslVersion = ImGuiGlslVersion(),
    };
    if (guiLayer.Initialize(config))
    {
        return true;
    }

    if (errorMessage != nullptr)
    {
        *errorMessage = "Dear ImGui OpenGL backend initialization failed";
    }
    return false;
}

void OpenGlGraphicsBackend::WaitForGpuIdle()
{
    if (_context != nullptr)
    {
        glFinish();
    }
}

void OpenGlGraphicsBackend::Shutdown()
{
    if (_context != nullptr)
    {
        SDL_GL_DeleteContext(_context);
        _context = nullptr;
    }

    _window = nullptr;
    _drawableWidth = 0;
    _drawableHeight = 0;
    _swapInterval = -1;
    _adapterName.clear();
    _versionString.clear();
}

void OpenGlGraphicsBackend::BeginFrame()
{
    glClearColor(0.035F, 0.045F, 0.055F, 1.0F);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
}

void OpenGlGraphicsBackend::BeginImGuiFrame(Gui::ImGuiLayer& guiLayer)
{
    guiLayer.BeginFrame();
}

void OpenGlGraphicsBackend::RenderImGui(Gui::ImGuiLayer& guiLayer)
{
    guiLayer.EndFrame();
}

void OpenGlGraphicsBackend::Present()
{
    if (_window != nullptr)
    {
        SDL_GL_SwapWindow(_window);
    }
}

void OpenGlGraphicsBackend::RefreshDrawableSize()
{
    if (_window == nullptr)
    {
        _drawableWidth = 0;
        _drawableHeight = 0;
        return;
    }

    SDL_GL_GetDrawableSize(_window, &_drawableWidth, &_drawableHeight);
}

bool OpenGlGraphicsBackend::SetVSyncEnabled(bool enabled)
{
    const int requestedInterval = enabled ? 1 : 0;
    if (_swapInterval == requestedInterval)
    {
        return true;
    }

    if (SDL_GL_SetSwapInterval(requestedInterval) != 0)
    {
        std::cerr << "SDL_GL_SetSwapInterval failed: " << SDL_GetError() << '\n';
        _swapInterval = SDL_GL_GetSwapInterval();
        return false;
    }

    _swapInterval = requestedInterval;
    return true;
}

bool OpenGlGraphicsBackend::VSyncEnabled() const
{
    return _swapInterval > 0;
}

int OpenGlGraphicsBackend::DrawableWidth() const
{
    return _drawableWidth;
}

int OpenGlGraphicsBackend::DrawableHeight() const
{
    return _drawableHeight;
}

bool OpenGlGraphicsBackend::UsesZeroToOneDepth() const
{
    return false;
}

const std::string& OpenGlGraphicsBackend::AdapterName() const
{
    return _adapterName;
}

const std::string& OpenGlGraphicsBackend::VersionString() const
{
    return _versionString;
}

bool OpenGlGraphicsBackend::SupportsGpuRoamLike() const
{
    return GLAD_GL_VERSION_4_3 != 0;
}

float OpenGlGraphicsBackend::LastGpuFrameMilliseconds() const
{
    return 0.0F;
}

float OpenGlGraphicsBackend::LastGpuWaitMilliseconds() const
{
    return 0.0F;
}

bool OpenGlGraphicsBackend::IsValid() const
{
    return _window != nullptr && _context != nullptr;
}
} // namespace ParallelRoam::Render
