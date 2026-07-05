#pragma once

#include "app/CameraController.h"
#include "app/InputState.h"
#include "gui/ImGuiLayer.h"
#include "platform/Window.h"
#include "render/TerrainRenderer.h"

#include <chrono>

namespace ParallelRoam::App
{
/// <summary>
/// 协调平台层、输入层、渲染层和 GUI 层的应用主循环
/// </summary>
class Application
{
public:
    Application() = default;
    ~Application();

    Application(const Application&) = delete;
    Application& operator=(const Application&) = delete;

    bool Initialize();

    /// <summary>
    /// 运行应用主循环，maxFrameCount 大于 0 时用于 smoke test 固定帧退出
    /// </summary>
    int Run(int maxFrameCount = -1);

    void Shutdown();

private:
    /// <summary>
    /// 单帧时间数据，同时保留真实帧耗时和模拟用钳制时间
    /// </summary>
    struct FrameTiming
    {
        // RawDeltaSeconds 保留真实帧时间，用于性能显示
        float RawDeltaSeconds{0.0F};

        // ClampedDeltaSeconds 只用于相机和模拟，避免卡顿后瞬移
        float ClampedDeltaSeconds{0.0F};
    };

    // GLAD 必须在 SDL OpenGL context 创建后加载函数指针
    [[nodiscard]] bool LoadOpenGL() const;

    // 同时计算真实帧时间和模拟用帧时间
    [[nodiscard]] FrameTiming ComputeFrameTiming();

    // 集中处理 SDL 事件，保证 GUI、输入状态和窗口尺寸看到同一批事件
    void PollEvents();

    // 阶段 1 渲染 height map terrain 和调试面板
    void RenderFrame(const FrameTiming& frameTiming);

    // 子系统按生命周期依赖顺序声明，析构和 Shutdown 更容易保持一致
    Platform::Window _window;
    InputState _input;
    CameraController _camera;
    Render::TerrainRenderer _terrainRenderer;
    Gui::ImGuiLayer _guiLayer;
    Render::TerrainRenderSettings _terrainSettings;
    Gui::TerrainPanelState _terrainPanelState;
    std::chrono::steady_clock::time_point _lastFrameTime{};
    bool _initialized{false};
    float _framesPerSecond{0.0F};
    float _frameTimeMilliseconds{0.0F};
};
} // 命名空间 ParallelRoam::App
