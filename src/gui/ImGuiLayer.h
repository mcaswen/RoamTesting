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
    float FrameTimeMilliseconds{0.0F};
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
    std::size_t RoamOriginalTriangleCount{0};
    std::size_t RoamSubdividedTriangleCount{0};
    std::size_t RoamRebuiltTriangleCount{0};

    // 下列统计直接来自 ClassicRoamStats
    std::size_t RoamActiveSplitCount{0};
    std::size_t RoamSplitCount{0};
    std::size_t RoamForcedSplitCount{0};
    std::size_t RoamMergeCount{0};

    // 裂缝风险用于判断当前最大深度是否过低
    std::size_t RoamCrackRiskCount{0};

    // 约束传播次数越高表示 diamond split 触发越明显
    std::size_t RoamConstraintPassCount{0};
    std::size_t RoamCandidatePeakCount{0};
    std::size_t RoamRejectedSplitCount{0};
    std::size_t RoamRejectedMergeCount{0};

    // 拓扑验证关闭时这两个值保持为 0
    std::size_t RoamTjunctionCount{0};
    std::size_t RoamInvalidNeighborCount{0};
    std::size_t RoamInvalidTopologyCount{0};

    // CPU 诊断字段来自统一算法 stats
    std::size_t RoamCpuWorkerCount{0};
    float RoamCpuUtilizationPercent{0.0F};

    // UI 直接展示算法层统计的各 pass 耗时
    float RoamUpdateMilliseconds{0.0F};
    float RoamSplitMilliseconds{0.0F};
    float RoamMergeMilliseconds{0.0F};
    float RoamEmitMilliseconds{0.0F};
    float RoamValidateMilliseconds{0.0F};
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
    int DebugColorMode{0};
    float DebugOverlayStrength{0.85F};
    bool UseClassicRoam{true};
    int RoamMaxDepth{14};

    // Split 和 Merge 使用双阈值减少相机移动时的抖动
    float RoamSplitThreshold{0.04F};
    float RoamMergeThreshold{0.02F};
    float RoamDistanceScale{24.0F};

    // SplitBudget 控制单次 Classic ROAM build 的最大 split 数
    int RoamSplitBudget{8192};

    // 局部约束是 Classic ROAM 消除裂缝的默认路径
    bool RoamEnableLocalConstraints{true};

    // 拓扑验证会全局扫描 active leaf，只在 debug 时打开
    bool RoamEnableTopologyValidation{false};

    // 光照字段保持在面板状态中，便于和 terrain 参数一起提交
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
    bool Initialize(SDL_Window* window, SDL_GLContext glContext, const char* glslVersion);

    void Shutdown();
    void ProcessEvent(const SDL_Event& event);
    void BeginFrame();

    /// <summary>
    /// 返回 true 表示 terrain 面板参数发生变化，调用方需要重新应用 renderer 设置
    /// </summary>
    [[nodiscard]] bool DrawDebugOverlay(const DebugOverlayData& data, TerrainPanelState& terrainState);

    void EndFrame();

private:
    // 防止未初始化或重复 Shutdown 时调用 backend 清理接口
    bool _initialized{false};
};
} // 命名空间 ParallelRoam::Gui
