#include "app/Application.h"

#include "platform/OpenGlCapabilities.h"

#include <glad/gl.h>
#include <glm/gtc/matrix_transform.hpp>

#include <SDL.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <exception>
#include <filesystem>
#include <iostream>
#include <string>
#include <utility>

namespace ParallelRoam::App
{
namespace
{
const std::array<std::filesystem::path, 2> HeightMapPaths{
    // 资源表顺序必须和 ImGui 高度图下拉框保持一致
    std::filesystem::path{"assets/heightmaps/Hm_Terrain_Test_129.pgm"},
    // Peking 513 用于更大输入规模下观察 ROAM 行为
    std::filesystem::path{"assets/heightmaps/Hm_Terrain_Peking_513.png"},
};

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

float SmoothStep(float value)
{
    // SmoothStep 让相机起停速度连续，避免 benchmark 首尾突变
    const float t = std::clamp(value, 0.0F, 1.0F);
    return t * t * (3.0F - 2.0F * t);
}

std::string BuildConfigurationName()
{
#if defined(PARALLEL_ROAM_BUILD_CONFIG)
    return PARALLEL_ROAM_BUILD_CONFIG;
#else
    return "Unknown";
#endif
}

std::pair<float, float> ComputeYawPitchForLookAt(const glm::vec3& position, const glm::vec3& target)
{
    // 目标点与相机重合时返回默认姿态，避免 atan2/asin 输入退化
    const glm::vec3 direction = target - position;
    const float lengthSquared = glm::dot(direction, direction);
    if (lengthSquared <= 0.000001F)
    {
        return {-90.0F, -18.0F};
    }

    const glm::vec3 normalizedDirection = glm::normalize(direction);
    // CameraController 的 yaw 约定是 -90 度看向 -Z
    const float yawDegrees = glm::degrees(std::atan2(normalizedDirection.z, normalizedDirection.x));
    const float pitchDegrees = glm::degrees(std::asin(std::clamp(normalizedDirection.y, -1.0F, 1.0F)));
    return {yawDegrees, pitchDegrees};
}

Render::TerrainRenderSettings ToRenderSettings(const Gui::TerrainPanelState& state)
{
    Render::TerrainRenderSettings settings{};
    settings.TerrainSize = state.TerrainSize;
    settings.HeightScale = state.HeightScale;
    settings.Wireframe = state.Wireframe;
    settings.DebugColorMode = static_cast<Render::TerrainDebugColorMode>(std::clamp(state.DebugColorMode, 0, 1));
    settings.DebugOverlayStrength = std::clamp(state.DebugOverlayStrength, 0.0F, 1.0F);
    settings.UseTerrainLod = state.UseTerrainLod;
    settings.TerrainLodAlgorithm = state.TerrainLodAlgorithm;
    settings.RoamMaxDepth = state.RoamMaxDepth;
    settings.RoamSplitThreshold = state.RoamSplitThreshold;
    settings.RoamMergeThreshold = state.RoamMergeThreshold;
    settings.RoamDistanceScale = state.RoamDistanceScale;
    settings.RoamEnableLocalConstraints = state.RoamEnableLocalConstraints;
    settings.RoamEnableTopologyValidation = state.RoamEnableTopologyValidation;
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

void Application::EnableGpuSmokeTest()
{
    _gpuSmokeTestEnabled = true;
    _terrainPanelState.UseTerrainLod = true;
    _terrainPanelState.TerrainLodAlgorithm = Algorithms::TerrainLodAlgorithmId::GpuRoamLike;
}

void Application::EnableAutomaticRuntimeBenchmark()
{
    _automaticRuntimeBenchmarkEnabled = true;
}

bool Application::Initialize()
{
    // SDL 窗口必须先创建，后续 GLAD 和 ImGui 都依赖当前 OpenGL context
    if (!_window.Create("Parallel ROAM", 1280, 720))
    {
        return false;
    }

    _input.SetWindowSize(_window.Width(), _window.Height());
    _terrainPanelState.VSyncEnabled = _window.VSyncEnabled();
    _terrainPanelState.HeightMapIndex = 0;

    // GLAD 加载失败时不能继续创建任何 OpenGL 对象
    if (!LoadOpenGL())
    {
        Shutdown();
        return false;
    }

    _terrainSettings = ToRenderSettings(_terrainPanelState);

    // 渲染器加载 Height Map、地表纹理和内置 shader
    std::string rendererError;
    if (!_terrainRenderer.Initialize(
            HeightMapPaths[static_cast<std::size_t>(_terrainPanelState.HeightMapIndex)],
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
    if (_automaticRuntimeBenchmarkEnabled)
    {
        StartRuntimeBenchmark();
    }

    while (!_input.IsQuitRequested())
    {
        const FrameTiming frameTiming = ComputeFrameTiming();

        // 鼠标位移是逐帧增量，必须在轮询事件前清零
        _input.BeginFrame();
        PollEvents();

        if (_input.IsKeyDown(SDL_SCANCODE_ESCAPE))
        {
            break;
        }

        if (_runtimeBenchmark.StartRequested && !_runtimeBenchmark.Active)
        {
            // UI 事件在 RenderFrame 里产生，下一轮主循环再启动测试更稳定
            _runtimeBenchmark.StartRequested = false;
            StartRuntimeBenchmark();
        }

        // benchmark 接管相机时不捕获鼠标，避免测试中途被用户输入污染
        _window.SetRelativeMouseMode(!_runtimeBenchmark.Active && _input.IsRightMouseDown());
        if (_runtimeBenchmark.Active)
        {
            PrepareRuntimeBenchmarkFrame(frameTiming);
        }
        else
        {
            _camera.Update(_input, frameTiming.ClampedDeltaSeconds);
        }

        RenderFrame(frameTiming);
        _window.SwapBuffers();
        CompleteRuntimeBenchmarkFrame();
        if (_automaticRuntimeBenchmarkEnabled && _automaticRuntimeBenchmarkCompleted)
        {
            break;
        }

        // smoke test 通过固定帧数退出，便于自动验证窗口和 GL context
        ++frameCount;
        if (maxFrameCount > 0 && frameCount >= maxFrameCount)
        {
            break;
        }
    }

    const bool automaticBenchmarkIncomplete =
        _automaticRuntimeBenchmarkEnabled && !_automaticRuntimeBenchmarkCompleted;
    const int exitCode =
        (_gpuSmokeTestFailed || _automaticRuntimeBenchmarkFailed || automaticBenchmarkIncomplete) ? 1 : 0;
    Shutdown();
    return exitCode;
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

Application::FrameTiming Application::ComputeFrameTiming()
{
    const auto now = std::chrono::steady_clock::now();
    const std::chrono::duration<float> delta = now - _lastFrameTime;
    _lastFrameTime = now;

    // 调试断点或窗口拖拽会造成异常大 delta，需要限制相机单帧位移
    constexpr float MaxDeltaSeconds = 0.1F;
    const float rawDeltaSeconds = std::max(delta.count(), 0.0F);

    FrameTiming frameTiming{};
    frameTiming.RawDeltaSeconds = rawDeltaSeconds;
    frameTiming.ClampedDeltaSeconds = std::min(rawDeltaSeconds, MaxDeltaSeconds);
    return frameTiming;
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

void Application::RenderFrame(const FrameTiming& frameTiming)
{
    _window.RefreshSize();

    // HiDPI 下 drawable 尺寸可能大于窗口逻辑尺寸，viewport 必须使用 drawable
    const int drawableWidth = std::max(_window.DrawableWidth(), 1);
    const int drawableHeight = std::max(_window.DrawableHeight(), 1);
    const float aspectRatio = static_cast<float>(drawableWidth) / static_cast<float>(drawableHeight);

    // FPS 和帧时间必须使用 raw delta，不能受相机 delta clamp 影响
    if (frameTiming.RawDeltaSeconds > 0.0F)
    {
        _framesPerSecond = 1.0F / frameTiming.RawDeltaSeconds;
        _frameTimeMilliseconds = frameTiming.RawDeltaSeconds * 1000.0F;
    }

    glClearColor(0.035F, 0.045F, 0.055F, 1.0F);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    _guiLayer.BeginFrame();

    const glm::vec3 cameraPosition = _camera.Position();
    if (_gpuSmokeTestEnabled)
    {
        _terrainRenderer.RequestMeshRebuild();
    }

    std::string meshUpdateError;
    if (!_terrainRenderer.UpdateForCamera(cameraPosition, &meshUpdateError))
    {
        if (_runtimeBenchmark.Active && !_runtimeBenchmark.Failed)
        {
            _runtimeBenchmark.Failed = true;
            _runtimeBenchmark.FailureMessage =
                meshUpdateError.empty() ? "Terrain rebuild failed during runtime benchmark" : meshUpdateError;
        }
        _gpuSmokeTestFailed = _gpuSmokeTestEnabled;
        if (meshUpdateError != _lastMeshUpdateError)
        {
            std::cerr << meshUpdateError << '\n';
            _lastMeshUpdateError = meshUpdateError;
        }
    }
    else
    {
        _lastMeshUpdateError.clear();
    }

    const Render::TerrainRenderStats terrainStats = _terrainRenderer.Stats();
    Gui::DebugOverlayData debugData{};
    debugData.FramesPerSecond = _framesPerSecond;
    debugData.FrameTimeMilliseconds = _frameTimeMilliseconds;
    debugData.WindowWidth = _window.Width();
    debugData.WindowHeight = _window.Height();
    debugData.DrawableWidth = drawableWidth;
    debugData.DrawableHeight = drawableHeight;
    debugData.VSyncEnabled = _terrainPanelState.VSyncEnabled;
    debugData.CameraPosition = cameraPosition;
    debugData.CameraYawDegrees = _camera.YawDegrees();
    debugData.CameraPitchDegrees = _camera.PitchDegrees();
    debugData.HeightMapWidth = terrainStats.HeightMapWidth;
    debugData.HeightMapHeight = terrainStats.HeightMapHeight;
    debugData.VertexCount = terrainStats.VertexCount;
    debugData.TriangleCount = terrainStats.TriangleCount;
    debugData.DrawCallCount = terrainStats.DrawCallCount;
    debugData.UseTerrainLod = terrainStats.UseTerrainLod;
    debugData.TerrainLodAlgorithm = terrainStats.TerrainLodAlgorithm;
    debugData.TerrainLodStatusMessage = terrainStats.TerrainLodStatusMessage;
    debugData.RoamNodeCount = terrainStats.RoamNodeCount;
    debugData.RoamOriginalTriangleCount = terrainStats.RoamOriginalTriangleCount;
    debugData.RoamSubdividedTriangleCount = terrainStats.RoamSubdividedTriangleCount;
    debugData.RoamRebuiltTriangleCount = terrainStats.RoamRebuiltTriangleCount;
    debugData.RoamActiveSplitCount = terrainStats.RoamActiveSplitCount;
    debugData.RoamSplitCount = terrainStats.RoamSplitCount;
    debugData.RoamForcedSplitCount = terrainStats.RoamForcedSplitCount;
    debugData.RoamMergeCount = terrainStats.RoamMergeCount;
    debugData.RoamCrackRiskCount = terrainStats.RoamCrackRiskCount;
    debugData.RoamConstraintPassCount = terrainStats.RoamConstraintPassCount;
    debugData.RoamCandidatePeakCount = terrainStats.RoamCandidatePeakCount;
    debugData.RoamRejectedSplitCount = terrainStats.RoamRejectedSplitCount;
    debugData.RoamRejectedMergeCount = terrainStats.RoamRejectedMergeCount;
    debugData.RoamTjunctionCount = terrainStats.RoamTjunctionCount;
    debugData.RoamInvalidNeighborCount = terrainStats.RoamInvalidNeighborCount;
    debugData.RoamInvalidTopologyCount = terrainStats.RoamInvalidTopologyCount;
    debugData.RoamCpuWorkerCount = terrainStats.RoamCpuWorkerCount;
    debugData.RoamCpuUtilizationPercent = terrainStats.RoamCpuUtilizationPercent;
    debugData.RoamTotalMilliseconds = terrainStats.RoamTotalMilliseconds;
    debugData.RoamUpdateMilliseconds = terrainStats.RoamUpdateMilliseconds;
    debugData.RoamCpuUploadMilliseconds = terrainStats.RoamCpuUploadMilliseconds;
    debugData.RoamSplitMilliseconds = terrainStats.RoamSplitMilliseconds;
    debugData.RoamMergeMilliseconds = terrainStats.RoamMergeMilliseconds;
    debugData.RoamEmitMilliseconds = terrainStats.RoamEmitMilliseconds;
    debugData.RoamValidateMilliseconds = terrainStats.RoamValidateMilliseconds;
    debugData.RoamGpuComputeMilliseconds = terrainStats.RoamGpuComputeMilliseconds;
    debugData.RoamGpuSnapshotBuildMilliseconds = terrainStats.RoamGpuSnapshotBuildMilliseconds;
    debugData.RoamGpuBufferAllocationMilliseconds = terrainStats.RoamGpuBufferAllocationMilliseconds;
    debugData.RoamGpuDispatchWallMilliseconds = terrainStats.RoamGpuDispatchWallMilliseconds;
    debugData.RoamGpuQueryWaitMilliseconds = terrainStats.RoamGpuQueryWaitMilliseconds;
    debugData.RoamGpuReadbackWaitMilliseconds = terrainStats.RoamGpuReadbackWaitMilliseconds;
    debugData.RoamRenderMilliseconds = terrainStats.RoamRenderMilliseconds;
    debugData.RoamCpuGpuUploadBytes = terrainStats.RoamCpuGpuUploadBytes;
    debugData.RoamCpuGpuReadbackBytes = terrainStats.RoamCpuGpuReadbackBytes;
    debugData.RoamMaxDepthSetting = terrainStats.RoamMaxDepthSetting;
    debugData.RoamMaxDepthReached = terrainStats.RoamMaxDepthReached;
    // benchmark 状态走 DebugOverlayData，GUI 不直接读取 Application 成员
    debugData.BenchmarkRunning = _runtimeBenchmark.Active;
    debugData.BenchmarkAlgorithmName = CurrentRuntimeBenchmarkAlgorithmName();
    debugData.BenchmarkProgress = RuntimeBenchmarkProgress();
    if (!_runtimeBenchmark.LastMarkdownPath.empty())
    {
        debugData.LastBenchmarkOutputPath = _runtimeBenchmark.LastMarkdownPath.string();
    }

    RecordRuntimeBenchmarkSample(frameTiming, terrainStats, cameraPosition);

    const bool previousVSyncEnabled = _terrainPanelState.VSyncEnabled;
    const int previousHeightMapIndex = _terrainPanelState.HeightMapIndex;
    if (_guiLayer.DrawDebugOverlay(debugData, _terrainPanelState))
    {
        ApplyTerrainPanelSettings();
    }

    if (previousVSyncEnabled != _terrainPanelState.VSyncEnabled)
    {
        // VSync 改变只更新窗口 swap interval，不触发 mesh 或算法重建
        ApplyWindowPanelSettings();
    }

    if (previousHeightMapIndex != _terrainPanelState.HeightMapIndex)
    {
        ApplyHeightMapSelection();
    }

    if (_terrainPanelState.StartBenchmarkRequested)
    {
        // 按钮请求被消费后立刻清零，避免下一帧重复启动
        _runtimeBenchmark.StartRequested = true;
        _terrainPanelState.StartBenchmarkRequested = false;
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

void Application::ApplyTerrainPanelSettings()
{
    _terrainSettings = ToRenderSettings(_terrainPanelState);

    std::string settingsError;
    if (!_terrainRenderer.ApplySettings(_terrainSettings, &settingsError))
    {
        std::cerr << settingsError << '\n';
    }
}

void Application::ApplyWindowPanelSettings()
{
    if (!_window.SetVSyncEnabled(_terrainPanelState.VSyncEnabled))
    {
        _terrainPanelState.VSyncEnabled = _window.VSyncEnabled();
    }
}

void Application::ApplyHeightMapSelection()
{
    // UI index 先钳制到资源表范围，防止未来增删选项时越界
    const int selectedIndex = std::clamp(
        _terrainPanelState.HeightMapIndex,
        0,
        static_cast<int>(HeightMapPaths.size()) - 1);
    _terrainPanelState.HeightMapIndex = selectedIndex;
    const std::filesystem::path& heightMapPath = HeightMapPaths[static_cast<std::size_t>(selectedIndex)];

    std::string heightMapError;
    if (!_terrainRenderer.LoadHeightMap(heightMapPath, &heightMapError))
    {
        std::cerr << heightMapError << '\n';
        // 加载失败时把 UI 回滚到 renderer 当前实际持有的高度图
        const auto currentPath = _terrainRenderer.HeightMapPath();
        const auto match = std::find_if(
            HeightMapPaths.begin(),
            HeightMapPaths.end(),
            [&currentPath](const std::filesystem::path& optionPath) {
                return optionPath == currentPath;
            });
        if (match != HeightMapPaths.end())
        {
            _terrainPanelState.HeightMapIndex = static_cast<int>(std::distance(HeightMapPaths.begin(), match));
        }
    }
}

void Application::StartRuntimeBenchmark()
{
    // 每轮 benchmark 都重新生成结果，保留上一次输出路径供 UI 展示
    _runtimeBenchmark.Active = true;
    _runtimeBenchmark.HasPreparedFirstFrame = false;
    _runtimeBenchmark.AlgorithmIndex = 0;
    _runtimeBenchmark.ElapsedSeconds = 0.0F;
    _runtimeBenchmark.Failed = false;
    _runtimeBenchmark.FailureMessage.clear();
    _runtimeBenchmark.Results.clear();
    _runtimeBenchmark.Notes.clear();
    _runtimeBenchmark.Notes.push_back("Build configuration: " + BuildConfigurationName());
    _runtimeBenchmark.AlgorithmSequence = {
        Algorithms::TerrainLodAlgorithmId::ClassicCpuRoam,
        Algorithms::TerrainLodAlgorithmId::DataOrientedCpuRoam,
    };
    const Platform::OpenGlGpuCapabilities gpuCapabilities = Platform::QueryOpenGlGpuCapabilities();
    if (gpuCapabilities.SupportsGpuRoamCompute())
    {
        _runtimeBenchmark.AlgorithmSequence.push_back(Algorithms::TerrainLodAlgorithmId::GpuRoamLike);
        _runtimeBenchmark.Notes.push_back(
            "GPU device: " + gpuCapabilities.RendererString + " (" + gpuCapabilities.VersionString + ")");
        _runtimeBenchmark.Notes.push_back(
            "GPU timing model: CPU DOD topology baseline plus GPU split-only, compaction, mesh emit and " +
            std::string{gpuCapabilities.SupportsIndirectDraw ? "indirect draw" : "buffer draw"} +
            "; GPU ms measures compute passes only");
    }
    else
    {
        _runtimeBenchmark.Notes.push_back(
            "GPU ROAM-like skipped: " + gpuCapabilities.GpuRoamComputeUnavailableReason());
    }
    _runtimeBenchmark.PreviousTerrainPanelState = _terrainPanelState;
    _runtimeBenchmark.PreviousTerrainPanelState.StartBenchmarkRequested = false;
    _runtimeBenchmark.PreviousCameraPose = CameraPose{
        _camera.Position(),
        _camera.YawDegrees(),
        _camera.PitchDegrees(),
    };
    _terrainPanelState.VSyncEnabled = false;
    ApplyWindowPanelSettings();
    _runtimeBenchmark.Notes.push_back(
        _terrainPanelState.VSyncEnabled ?
            "VSync: enabled (disable request was not accepted)" :
            "VSync: disabled during benchmark");

    BeginRuntimeBenchmarkAlgorithm();
}

void Application::BeginRuntimeBenchmarkAlgorithm()
{
    if (_runtimeBenchmark.AlgorithmIndex >= _runtimeBenchmark.AlgorithmSequence.size())
    {
        FinishRuntimeBenchmark();
        return;
    }

    const Algorithms::TerrainLodAlgorithmId algorithmId =
        _runtimeBenchmark.AlgorithmSequence[_runtimeBenchmark.AlgorithmIndex];

    RuntimeBenchmarkAlgorithmResult result{};
    result.AlgorithmId = algorithmId;
    result.AlgorithmName = RuntimeBenchmarkAlgorithmDisplayName(algorithmId);
    result.Samples.reserve(720);
    _runtimeBenchmark.Results.push_back(std::move(result));

    const float halfTerrainSize = _terrainPanelState.TerrainSize * 0.5F;
    const float cameraHeight = std::max(3.0F, _terrainPanelState.HeightScale * 1.5F);
    // Z+ 边中点到中心的路径便于和固定朝向一起解释
    _runtimeBenchmark.StartPosition = glm::vec3{0.0F, cameraHeight, halfTerrainSize};
    _runtimeBenchmark.EndPosition = glm::vec3{0.0F, cameraHeight, 0.0F};
    const auto [yawDegrees, pitchDegrees] =
        ComputeYawPitchForLookAt(_runtimeBenchmark.StartPosition, glm::vec3{0.0F, 0.0F, 0.0F});
    _runtimeBenchmark.YawDegrees = yawDegrees;
    _runtimeBenchmark.PitchDegrees = pitchDegrees;
    _runtimeBenchmark.ElapsedSeconds = 0.0F;
    _runtimeBenchmark.HasPreparedFirstFrame = false;

    _terrainPanelState.UseTerrainLod = true;
    _terrainPanelState.TerrainLodAlgorithm = algorithmId;
    _terrainPanelState.StartBenchmarkRequested = false;
    // ApplySettings 先切换算法，再 reset 可保证下一帧从干净拓扑开始
    ApplyTerrainPanelSettings();
    _terrainRenderer.ResetTerrainLodAlgorithm();
    _terrainRenderer.RequestMeshRebuild();
    _camera.SetPose(_runtimeBenchmark.StartPosition, _runtimeBenchmark.YawDegrees, _runtimeBenchmark.PitchDegrees);
}

void Application::PrepareRuntimeBenchmarkFrame(const FrameTiming& frameTiming)
{
    if (!_runtimeBenchmark.Active)
    {
        return;
    }

    if (_runtimeBenchmark.HasPreparedFirstFrame)
    {
        // 第一帧已在 t=0 采样，后续帧再推进时间
        const float deltaSeconds = std::max(frameTiming.RawDeltaSeconds, 0.0F);
        _runtimeBenchmark.ElapsedSeconds =
            std::min(_runtimeBenchmark.ElapsedSeconds + deltaSeconds, _runtimeBenchmark.DurationSeconds);
    }
    else
    {
        _runtimeBenchmark.HasPreparedFirstFrame = true;
    }

    const float t = _runtimeBenchmark.ElapsedSeconds / _runtimeBenchmark.DurationSeconds;
    // 只平滑位置，不旋转相机，保证每个算法看到同一条视点路径
    const glm::vec3 cameraPosition =
        glm::mix(_runtimeBenchmark.StartPosition, _runtimeBenchmark.EndPosition, SmoothStep(t));
    _camera.SetPose(cameraPosition, _runtimeBenchmark.YawDegrees, _runtimeBenchmark.PitchDegrees);
    _terrainRenderer.RequestMeshRebuild();
}

void Application::CompleteRuntimeBenchmarkFrame()
{
    if (!_runtimeBenchmark.Active)
    {
        return;
    }

    if (_runtimeBenchmark.Failed)
    {
        FinishRuntimeBenchmark();
        return;
    }

    if (_runtimeBenchmark.ElapsedSeconds < _runtimeBenchmark.DurationSeconds)
    {
        return;
    }

    ++_runtimeBenchmark.AlgorithmIndex;
    if (_runtimeBenchmark.AlgorithmIndex < _runtimeBenchmark.AlgorithmSequence.size())
    {
        // 当前算法跑满 10 秒后立即切到下一个算法
        BeginRuntimeBenchmarkAlgorithm();
        return;
    }

    FinishRuntimeBenchmark();
}

void Application::RecordRuntimeBenchmarkSample(
    const FrameTiming& frameTiming,
    const Render::TerrainRenderStats& terrainStats,
    const glm::vec3& cameraPosition)
{
    if (!_runtimeBenchmark.Active || _runtimeBenchmark.Failed || _runtimeBenchmark.Results.empty())
    {
        return;
    }

    RuntimeBenchmarkSample sample{};
    sample.BuildConfiguration = BuildConfigurationName();
    sample.VSyncEnabled = _terrainPanelState.VSyncEnabled;
    sample.TimeSeconds = _runtimeBenchmark.ElapsedSeconds;
    sample.CameraPosition = cameraPosition;
    // RawDeltaSeconds 是真实帧耗时，ClampedDeltaSeconds 只适合模拟
    sample.FrameMilliseconds = frameTiming.RawDeltaSeconds * 1000.0F;
    sample.Stats = terrainStats;
    _runtimeBenchmark.Results.back().Samples.push_back(sample);
}

void Application::FinishRuntimeBenchmark()
{
    bool reportSucceeded = false;
    if (_runtimeBenchmark.Failed)
    {
        std::cerr << "Runtime benchmark aborted: " << _runtimeBenchmark.FailureMessage << '\n';
    }
    else
    {
        try
        {
            const RuntimeBenchmarkReportPaths paths =
                WriteRuntimeBenchmarkReport(_runtimeBenchmark.Results, _runtimeBenchmark.Notes);
            // 输出路径留在状态里，下一帧 UI 可以继续展示给用户
            _runtimeBenchmark.LastMarkdownPath = paths.MarkdownPath;
            _runtimeBenchmark.LastCsvPath = paths.CsvPath;
            std::cout << "Runtime benchmark report: " << paths.MarkdownPath << '\n';
            std::cout << "Runtime benchmark csv: " << paths.CsvPath << '\n';
            reportSucceeded = true;
        }
        catch (const std::exception& exception)
        {
            std::cerr << exception.what() << '\n';
        }
    }

    const Gui::TerrainPanelState previousTerrainPanelState = _runtimeBenchmark.PreviousTerrainPanelState;
    const CameraPose previousCameraPose = _runtimeBenchmark.PreviousCameraPose;
    // 先退出 Active，再恢复 UI 状态，避免 DrawDebugOverlay 继续锁定控件
    _runtimeBenchmark.Active = false;
    _runtimeBenchmark.HasPreparedFirstFrame = false;
    _runtimeBenchmark.ElapsedSeconds = 0.0F;

    _terrainPanelState = previousTerrainPanelState;
    _terrainPanelState.StartBenchmarkRequested = false;
    ApplyWindowPanelSettings();
    _camera.SetPose(previousCameraPose.Position, previousCameraPose.YawDegrees, previousCameraPose.PitchDegrees);
    // 恢复设置后强制重建一次，防止画面停留在 benchmark 的算法 mesh
    ApplyTerrainPanelSettings();
    _terrainRenderer.ResetTerrainLodAlgorithm();
    _terrainRenderer.RequestMeshRebuild();

    if (_automaticRuntimeBenchmarkEnabled)
    {
        _automaticRuntimeBenchmarkCompleted = true;
        _automaticRuntimeBenchmarkFailed = !reportSucceeded;
    }
}

std::string Application::CurrentRuntimeBenchmarkAlgorithmName() const
{
    if (!_runtimeBenchmark.Active || _runtimeBenchmark.AlgorithmIndex >= _runtimeBenchmark.AlgorithmSequence.size())
    {
        return {};
    }

    return RuntimeBenchmarkAlgorithmDisplayName(_runtimeBenchmark.AlgorithmSequence[_runtimeBenchmark.AlgorithmIndex]);
}

float Application::RuntimeBenchmarkProgress() const
{
    if (!_runtimeBenchmark.Active || _runtimeBenchmark.AlgorithmSequence.empty())
    {
        // 非运行状态下 UI 进度条保持空值
        return 0.0F;
    }

    // 进度按算法数量归一化，顶部条展示整轮 benchmark 进度
    const float localProgress = std::clamp(
        _runtimeBenchmark.ElapsedSeconds / _runtimeBenchmark.DurationSeconds,
        0.0F,
        1.0F);
    const float completedAlgorithms = static_cast<float>(_runtimeBenchmark.AlgorithmIndex);
    // AlgorithmSequence 非空已在函数入口确认
    const float algorithmCount = static_cast<float>(_runtimeBenchmark.AlgorithmSequence.size());
    return (completedAlgorithms + localProgress) / algorithmCount;
}
} // 命名空间 ParallelRoam::App
