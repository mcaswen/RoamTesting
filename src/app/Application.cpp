#include "app/Application.h"

#include <glad/gl.h>
#include <glm/gtc/matrix_transform.hpp>

#include <SDL.h>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <string>

namespace ParallelRoam::App
{
namespace
{
// SDL 返回的是平台相关函数指针，GLAD 需要统一包装成 loader 回调
GLADapiproc LoadOpenGLProc(const char* name)
{
    return reinterpret_cast<GLADapiproc>(SDL_GL_GetProcAddress(name));
}

// macOS 桌面 OpenGL 最高暴露 4.1 core，因此 shader 版本需要单独选择
const char* ImGuiGlslVersion()
{
#if defined(__APPLE__)
    return "#version 410";
#else
    return "#version 430";
#endif
}

Render::TerrainRenderSettings ToRenderSettings(const Gui::TerrainPanelState& state)
{
    Render::TerrainRenderSettings settings{};
    settings.TerrainSize = state.TerrainSize;
    settings.HeightScale = state.HeightScale;
    settings.Wireframe = state.Wireframe;
    settings.UseClassicRoam = state.UseClassicRoam;
    settings.RoamMaxDepth = state.RoamMaxDepth;
    settings.RoamSplitThreshold = state.RoamSplitThreshold;
    settings.RoamMergeThreshold = state.RoamMergeThreshold;
    settings.RoamDistanceScale = state.RoamDistanceScale;
    settings.RoamEnableCrackFix = state.RoamEnableCrackFix;
    settings.LightDirection = state.LightDirection;
    settings.LightColor = state.LightColor;
    settings.AmbientStrength = state.AmbientStrength;
    settings.DiffuseStrength = state.DiffuseStrength;
    settings.SpecularStrength = state.SpecularStrength;
    return settings;
}
} // 匿名命名空间

Application::~Application()
{
    Shutdown();
}

bool Application::Initialize()
{
    // SDL 窗口必须先创建，后续 GLAD 和 ImGui 都依赖当前 OpenGL context
    if (!_window.Create("Parallel ROAM", 1280, 720))
    {
        return false;
    }

    _input.SetWindowSize(_window.Width(), _window.Height());

    // GLAD 加载失败时不能继续创建任何 OpenGL 对象
    if (!LoadOpenGL())
    {
        Shutdown();
        return false;
    }

    _terrainSettings = ToRenderSettings(_terrainPanelState);

    // 阶段 1 渲染器加载 Height Map、地表纹理和内置 shader
    std::string rendererError;
    if (!_terrainRenderer.Initialize(
            std::filesystem::path{"assets/heightmaps/Hm_Terrain_Test_129.pgm"},
            std::filesystem::path{"assets/textures/Tex_Terrain_Debug_Diffuse.ppm"},
            _terrainSettings,
            &rendererError))
    {
        std::cerr << rendererError << '\n';
        Shutdown();
        return false;
    }

    // ImGui backend 绑定当前 SDL window 和 OpenGL context
    if (!_guiLayer.Initialize(_window.NativeWindow(), _window.GlContext(), ImGuiGlslVersion()))
    {
        std::cerr << "Dear ImGui initialization failed.\n";
        Shutdown();
        return false;
    }

    _lastFrameTime = std::chrono::steady_clock::now();
    _initialized = true;
    return true;
}

int Application::Run(int maxFrameCount)
{
    if (!_initialized)
    {
        return 1;
    }

    int frameCount = 0;
    while (!_input.IsQuitRequested())
    {
        const float deltaSeconds = ComputeDeltaSeconds();

        // 鼠标位移是逐帧增量，必须在轮询事件前清零
        _input.BeginFrame();
        PollEvents();

        if (_input.IsKeyDown(SDL_SCANCODE_ESCAPE))
        {
            break;
        }

        // 只在右键按住时启用相对鼠标模式，避免普通 GUI 操作丢失光标
        _window.SetRelativeMouseMode(_input.IsRightMouseDown());
        _camera.Update(_input, deltaSeconds);
        RenderFrame(deltaSeconds);
        _window.SwapBuffers();

        // smoke test 通过固定帧数退出，便于自动验证窗口和 GL context
        ++frameCount;
        if (maxFrameCount > 0 && frameCount >= maxFrameCount)
        {
            break;
        }
    }

    Shutdown();
    return 0;
}

void Application::Shutdown()
{
    // 允许析构函数和显式 Shutdown 重复调用，避免双重释放
    if (!_initialized && !_window.IsValid())
    {
        return;
    }

    // 资源释放顺序与依赖关系相反，ImGui 先于窗口和 context 释放
    _guiLayer.Shutdown();
    _terrainRenderer.Shutdown();
    _window.Destroy();
    _initialized = false;
}

bool Application::LoadOpenGL() const
{
    // gladLoadGL 只能在当前线程已有 OpenGL context 时调用
    const int version = gladLoadGL(LoadOpenGLProc);
    if (version == 0)
    {
        std::cerr << "gladLoadGL failed.\n";
        return false;
    }

    std::cout << "OpenGL loaded: " << GLAD_VERSION_MAJOR(version) << '.' << GLAD_VERSION_MINOR(version) << '\n';
    std::cout << "OpenGL renderer: " << glGetString(GL_RENDERER) << '\n';
    std::cout << "OpenGL version: " << glGetString(GL_VERSION) << '\n';
    return true;
}

float Application::ComputeDeltaSeconds()
{
    const auto now = std::chrono::steady_clock::now();
    const std::chrono::duration<float> delta = now - _lastFrameTime;
    _lastFrameTime = now;

    // 调试断点或窗口拖拽会造成异常大 delta，需要限制相机单帧位移
    constexpr float MaxDeltaSeconds = 0.1F;
    return std::clamp(delta.count(), 0.0F, MaxDeltaSeconds);
}

void Application::PollEvents()
{
    SDL_Event event{};
    while (SDL_PollEvent(&event) != 0)
    {
        // 三个层级消费同一事件，后续可在这里接入事件总线
        _guiLayer.ProcessEvent(event);
        _input.HandleEvent(event);
        _window.ProcessEvent(event);
    }
}

void Application::RenderFrame(float deltaSeconds)
{
    _window.RefreshSize();

    // HiDPI 下 drawable 尺寸可能大于窗口逻辑尺寸，viewport 必须使用 drawable
    const int drawableWidth = std::max(_window.DrawableWidth(), 1);
    const int drawableHeight = std::max(_window.DrawableHeight(), 1);
    const float aspectRatio = static_cast<float>(drawableWidth) / static_cast<float>(drawableHeight);

    // 当前先显示瞬时 FPS，后续 profiling 模块再替换为滑动平均
    if (deltaSeconds > 0.0F)
    {
        _framesPerSecond = 1.0F / deltaSeconds;
    }

    glClearColor(0.035F, 0.045F, 0.055F, 1.0F);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    _guiLayer.BeginFrame();

    const glm::vec3 cameraPosition = _camera.Position();
    std::string meshUpdateError;
    if (!_terrainRenderer.UpdateForCamera(cameraPosition, &meshUpdateError))
    {
        std::cerr << meshUpdateError << '\n';
    }

    const Render::TerrainRenderStats terrainStats = _terrainRenderer.Stats();
    Gui::DebugOverlayData debugData{};
    debugData.FramesPerSecond = _framesPerSecond;
    debugData.WindowWidth = _window.Width();
    debugData.WindowHeight = _window.Height();
    debugData.DrawableWidth = drawableWidth;
    debugData.DrawableHeight = drawableHeight;
    debugData.CameraPosition = cameraPosition;
    debugData.CameraYawDegrees = _camera.YawDegrees();
    debugData.CameraPitchDegrees = _camera.PitchDegrees();
    debugData.HeightMapWidth = terrainStats.HeightMapWidth;
    debugData.HeightMapHeight = terrainStats.HeightMapHeight;
    debugData.VertexCount = terrainStats.VertexCount;
    debugData.TriangleCount = terrainStats.TriangleCount;
    debugData.DrawCallCount = terrainStats.DrawCallCount;
    debugData.UseClassicRoam = terrainStats.UseClassicRoam;
    debugData.RoamNodeCount = terrainStats.RoamNodeCount;
    debugData.RoamSplitCount = terrainStats.RoamSplitCount;
    debugData.RoamForcedSplitCount = terrainStats.RoamForcedSplitCount;
    debugData.RoamMergeCount = terrainStats.RoamMergeCount;
    debugData.RoamCrackRiskCount = terrainStats.RoamCrackRiskCount;
    debugData.RoamConstraintPassCount = terrainStats.RoamConstraintPassCount;
    debugData.RoamMaxDepthReached = terrainStats.RoamMaxDepthReached;

    if (_guiLayer.DrawDebugOverlay(debugData, _terrainPanelState))
    {
        _terrainSettings = ToRenderSettings(_terrainPanelState);

        std::string settingsError;
        if (!_terrainRenderer.ApplySettings(_terrainSettings, &settingsError))
        {
            std::cerr << settingsError << '\n';
        }
    }

    Render::RenderContext renderContext{};
    renderContext.View = _camera.GetViewMatrix();
    renderContext.Projection = glm::perspective(glm::radians(60.0F), aspectRatio, 0.05F, 1000.0F);
    renderContext.CameraPosition = cameraPosition;
    renderContext.DrawableWidth = drawableWidth;
    renderContext.DrawableHeight = drawableHeight;

    // terrain renderer 消费相机矩阵和 UI 参数，不直接处理输入事件
    _terrainRenderer.Render(renderContext);
    _guiLayer.EndFrame();
}
} // 命名空间 ParallelRoam::App
