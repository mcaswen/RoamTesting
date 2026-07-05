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

    /// <summary>
    /// 初始化窗口、OpenGL、阶段 1 terrain renderer 和 ImGui
    /// </summary>
    bool Initialize();

    /// <summary>
    /// 运行应用主循环
    /// </summary>
    /// <param name="maxFrameCount">大于 0 时在指定帧数后自动退出，供 smoke test 使用。</param>
    int Run(int maxFrameCount = -1);

    /// <summary>
    /// 按依赖顺序释放 GUI、渲染器、窗口和 SDL 资源
    /// </summary>
    void Shutdown();

private:
    // GLAD 必须在 SDL OpenGL context 创建后加载函数指针
    [[nodiscard]] bool LoadOpenGL() const;

    // 限制 delta time 峰值，避免调试暂停后相机瞬移
    [[nodiscard]] float ComputeDeltaSeconds();

    // 集中处理 SDL 事件，保证 GUI、输入状态和窗口尺寸看到同一批事件
    void PollEvents();

    // 阶段 1 渲染 height map terrain 和调试面板
    void RenderFrame(float deltaSeconds);

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
};
} // 命名空间 ParallelRoam::App
