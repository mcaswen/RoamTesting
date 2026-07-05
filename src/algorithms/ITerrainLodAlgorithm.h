#pragma once

#include "terrain/HeightMap.h"
#include "terrain/TerrainMeshBuilder.h"

#include <glm/glm.hpp>

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace ParallelRoam::Algorithms
{
/// <summary>
/// 标识当前 terrain LOD 算法实现，供 UI、benchmark 和日志输出使用
/// </summary>
enum class TerrainLodAlgorithmId
{
    ClassicCpuRoam,
    DataOrientedCpuRoam,
    GpuRoamLike,
};

/// <summary>
/// terrain LOD 算法的展示名称和简短描述
/// </summary>
struct TerrainLodAlgorithmInfo
{
    TerrainLodAlgorithmId Id{TerrainLodAlgorithmId::ClassicCpuRoam};
    std::string_view Name;
    std::string_view DisplayName;
    std::string_view Description;
};

/// <summary>
/// 描述某个 terrain LOD 算法当前可输出的渲染路径和拓扑能力
/// </summary>
struct TerrainLodAlgorithmCapabilities
{
    bool SupportsCpuMeshOutput{false};
    bool SupportsGpuDrivenRendering{false};
    bool SupportsSplit{false};
    bool SupportsMerge{false};
    bool SupportsCrackFix{false};
    bool SupportsTopologyValidation{false};
};

/// <summary>
/// 三种 ROAM 算法共享的运行参数，benchmark 和 renderer 使用同一套字段做公平对比
/// </summary>
struct TerrainLodSettings
{
    float TerrainSize{30.0F};
    float HeightScale{4.0F};
    int MaxDepth{14};
    float SplitThreshold{0.04F};
    float MergeThreshold{0.02F};
    float DistanceScale{24.0F};
    std::size_t SplitBudget{8192};
    bool EnableLocalConstraints{true};
    bool EnableTopologyValidation{false};
};

/// <summary>
/// 单帧 LOD 构建输入，固定高度图、相机位置和统一算法参数
/// </summary>
struct TerrainLodBuildInput
{
    const Terrain::HeightMap* HeightMap{nullptr};
    glm::vec3 CameraPosition{0.0F};
    TerrainLodSettings Settings;
};

/// <summary>
/// 算法渲染输出模式，区分 CPU mesh、GPU buffer 和 GPU driven 路径
/// </summary>
enum class TerrainLodRenderMode
{
    CpuMesh,
    GpuBuffers,
    GpuIndirect,
    DebugOnly,
};

/// <summary>
/// 算法输出给 renderer 或 benchmark 的统一渲染数据包
/// </summary>
struct TerrainLodRenderPacket
{
    TerrainLodRenderMode Mode{TerrainLodRenderMode::CpuMesh};
    Terrain::TerrainMeshData CpuMesh;
    std::uint32_t GpuVertexBufferId{0};
    std::uint32_t GpuIndexBufferId{0};
    std::uint32_t ActiveLeafBufferId{0};
    std::uint32_t IndirectDrawBufferId{0};
    std::size_t ActiveTriangleCount{0};
    std::size_t IndexCount{0};
};

/// <summary>
/// 跨 Classic / Data-Oriented / GPU 版本共享的统计字段，用于 UI 展示、回归测试和 CSV 输出
/// </summary>
struct TerrainLodStats
{
    std::size_t ActiveTriangleCount{0};
    std::size_t ActiveNodeCount{0};
    std::size_t OriginalTriangleCount{0};
    std::size_t SubdividedTriangleCount{0};
    std::size_t RebuiltTriangleCount{0};
    std::size_t ActiveSplitCount{0};
    std::size_t SplitCount{0};
    std::size_t ForcedSplitCount{0};
    std::size_t MergeCount{0};
    std::size_t CrackRiskCount{0};
    std::size_t ConstraintPassCount{0};
    std::size_t CandidatePeakCount{0};
    std::size_t RejectedSplitCount{0};
    std::size_t RejectedMergeCount{0};
    std::size_t TjunctionCount{0};
    std::size_t InvalidNeighborCount{0};
    std::size_t InvalidTopologyCount{0};
    std::size_t CpuGpuUploadBytes{0};
    std::size_t CpuGpuReadbackBytes{0};
    // CpuWorkerCount 表示本次 CPU LOD build 的实际并行宽度
    std::size_t CpuWorkerCount{0};
    float CpuUpdateMilliseconds{0.0F};
    // CpuUtilizationPercent 按单核 100% 口径记录进程 CPU 占用
    float CpuUtilizationPercent{0.0F};
    float CpuErrorEvalMilliseconds{0.0F};
    float CpuDecisionMilliseconds{0.0F};
    float CpuTopologyMilliseconds{0.0F};
    float CpuCollectMilliseconds{0.0F};
    float CpuMeshBuildMilliseconds{0.0F};
    float CpuUploadMilliseconds{0.0F};
    float GpuComputeMilliseconds{0.0F};
    float RenderMilliseconds{0.0F};
    float SplitMilliseconds{0.0F};
    float MergeMilliseconds{0.0F};
    float EmitMilliseconds{0.0F};
    float ValidateMilliseconds{0.0F};
    int MaxActiveDepth{0};
};

/// <summary>
/// 地形 LOD 算法的统一边界，Classic / Data-Oriented / GPU 版本都通过它接入 renderer 和 benchmark
/// </summary>
class ITerrainLodAlgorithm
{
public:
    virtual ~ITerrainLodAlgorithm() = default;

    [[nodiscard]] virtual TerrainLodAlgorithmInfo Info() const = 0;
    [[nodiscard]] virtual TerrainLodAlgorithmCapabilities Capabilities() const = 0;

    /// <summary>
    /// 根据固定输入构建当前帧统一渲染包，失败时通过 errorMessage 暴露可诊断原因
    /// </summary>
    [[nodiscard]] virtual bool BuildRenderData(
        const TerrainLodBuildInput& input,
        TerrainLodRenderPacket& outPacket,
        std::string* errorMessage) = 0;

    [[nodiscard]] virtual const TerrainLodStats& Stats() const = 0;
    virtual void Reset() = 0;
};
} // 命名空间 ParallelRoam::Algorithms
