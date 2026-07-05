#pragma once

#include <SDL.h>
#include <glm/glm.hpp>

#include <cstddef>

namespace ParallelRoam::Gui
{
/// <summary>
/// 调试面板需要展示的单帧状态
/// </summary>
struct DebugOverlayData
{
    float FramesPerSecond{0.0F};
    int WindowWidth{0};
    int WindowHeight{0};
    int DrawableWidth{0};
    int DrawableHeight{0};
    glm::vec3 CameraPosition{0.0F};
    float CameraYawDegrees{0.0F};
    float CameraPitchDegrees{0.0F};
    int HeightMapWidth{0};
    int HeightMapHeight{0};
    std::size_t VertexCount{0};
    std::size_t TriangleCount{0};
    int DrawCallCount{0};
    bool UseClassicRoam{false};
    std::size_t RoamNodeCount{0};

    // 下列统计直接来自 ClassicRoamStats
    std::size_t RoamSplitCount{0};
    std::size_t RoamForcedSplitCount{0};
    std::size_t RoamMergeCount{0};

    // 裂缝风险用于判断当前最大深度是否过低
    std::size_t RoamCrackRiskCount{0};

    // 约束传播次数越高表示 diamond split 触发越明显
    std::size_t RoamConstraintPassCount{0};
    int RoamMaxDepthReached{0};
};

/// <summary>
/// GUI 可编辑的 terrain 渲染参数
/// </summary>
struct TerrainPanelState
{
    float TerrainSize{30.0F};
    float HeightScale{4.0F};
    bool Wireframe{false};
    bool UseClassicRoam{true};
    int RoamMaxDepth{8};

    // Split 和 Merge 使用双阈值减少相机移动时的抖动
    float RoamSplitThreshold{0.16F};
    float RoamMergeThreshold{0.08F};
    float RoamDistanceScale{24.0F};

    // 关闭后可观察缺少 diamond split 时的裂缝风险
    bool RoamEnableCrackFix{true};
    glm::vec3 LightDirection{-0.45F, -1.0F, -0.35F};
    glm::vec3 LightColor{1.0F, 0.96F, 0.88F};
    float AmbientStrength{0.28F};
    float DiffuseStrength{0.85F};
    float SpecularStrength{0.18F};
};

/// <summary>
/// 面向运行时诊断的 Dear ImGui 集成层
/// </summary>
class ImGuiLayer
{
public:
    /// <summary>
    /// 初始化 ImGui context 和 SDL2/OpenGL3 backend
    /// </summary>
    /// <param name="window">SDL 窗口。</param>
    /// <param name="glContext">SDL OpenGL context。</param>
    /// <param name="glslVersion">OpenGL backend 使用的 GLSL 版本字符串。</param>
    bool Initialize(SDL_Window* window, SDL_GLContext glContext, const char* glslVersion);

    /// <summary>
    /// 释放 ImGui backend 和 context
    /// </summary>
    void Shutdown();

    /// <summary>
    /// 将 SDL 事件转交给 ImGui backend
    /// </summary>
    /// <param name="event">SDL 事件。</param>
    void ProcessEvent(const SDL_Event& event);

    /// <summary>
    /// 开始新一帧 ImGui 绘制
    /// </summary>
    void BeginFrame();

    /// <summary>
    /// 绘制阶段 1 调试和 terrain 控制面板
    /// </summary>
    /// <param name="data">当前帧调试数据。</param>
    /// <param name="terrainState">可编辑的 terrain 参数。</param>
    [[nodiscard]] bool DrawDebugOverlay(const DebugOverlayData& data, TerrainPanelState& terrainState);

    /// <summary>
    /// 提交并渲染 ImGui draw data
    /// </summary>
    void EndFrame();

private:
    // 防止未初始化或重复 Shutdown 时调用 backend 清理接口
    bool _initialized{false};
};
} // 命名空间 ParallelRoam::Gui
