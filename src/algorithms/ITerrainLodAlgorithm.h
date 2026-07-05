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
enum class TerrainLodAlgorithmId
{
    ClassicCpuRoam,
    DataOrientedCpuRoam,
    GpuRoamLike,
};

struct TerrainLodAlgorithmInfo
{
    TerrainLodAlgorithmId Id{TerrainLodAlgorithmId::ClassicCpuRoam};
    std::string_view Name;
    std::string_view DisplayName;
    std::string_view Description;
};

struct TerrainLodAlgorithmCapabilities
{
    bool SupportsCpuMeshOutput{false};
    bool SupportsGpuDrivenRendering{false};
    bool SupportsSplit{false};
    bool SupportsMerge{false};
    bool SupportsCrackFix{false};
    bool SupportsTopologyValidation{false};
};

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

struct TerrainLodBuildInput
{
    const Terrain::HeightMap* HeightMap{nullptr};
    glm::vec3 CameraPosition{0.0F};
    TerrainLodSettings Settings;
};

enum class TerrainLodRenderMode
{
    CpuMesh,
    GpuBuffers,
    GpuIndirect,
    DebugOnly,
};

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
    float CpuUpdateMilliseconds{0.0F};
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
    /// 根据固定输入构建当前帧统一渲染包
    /// </summary>
    /// <param name="input">高度图、相机和统一 LOD 配置。</param>
    /// <param name="outPacket">算法输出的 CPU mesh 或 GPU 资源描述。</param>
    /// <param name="errorMessage">失败时写入错误信息。</param>
    [[nodiscard]] virtual bool BuildRenderData(
        const TerrainLodBuildInput& input,
        TerrainLodRenderPacket& outPacket,
        std::string* errorMessage) = 0;

    [[nodiscard]] virtual const TerrainLodStats& Stats() const = 0;
    virtual void Reset() = 0;
};
} // 命名空间 ParallelRoam::Algorithms
