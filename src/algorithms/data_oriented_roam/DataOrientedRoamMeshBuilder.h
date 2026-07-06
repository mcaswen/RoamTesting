#pragma once

#include "terrain/HeightMap.h"
#include "terrain/TerrainMeshBuilder.h"

#include <glm/glm.hpp>

#include <memory>
#include <cstddef>

namespace ParallelRoam::Algorithms::DataOrientedRoam
{
struct DataOrientedRoamState;
class DataOrientedRoamThreadPool;

/// <summary>
/// 使用 UV 空间表达一个 ROAM 三角形域，避免节点池重复保存世界空间顶点
/// </summary>
struct TriangleDomain
{
    glm::vec2 A{0.0F};
    glm::vec2 B{0.0F};
    glm::vec2 C{0.0F};
};

/// <summary>
/// Data-Oriented CPU ROAM 的单帧细分、合并和拓扑验证参数
/// </summary>
struct DataOrientedRoamSettings
{
    int MaxDepth{14};
    float SplitThreshold{0.04F};
    float MergeThreshold{0.02F};
    float DistanceScale{24.0F};
    // 0 自动选择 worker 数 1 保持串行评估
    // 候选扫描也复用这个并行宽度设置
    std::size_t ErrorEvaluationWorkerCount{0};
    bool EnableLocalConstraints{true};
    bool EnableTopologyValidation{false};
};

/// <summary>
/// Data-Oriented CPU ROAM 的运行统计，补充节点池容量以观察预分配效果
/// </summary>
struct DataOrientedRoamStats
{
    std::size_t NodeCount{0};
    std::size_t ReservedNodeCapacity{0};
    std::size_t NodeStorageBytes{0};
    std::size_t NodeStorageArrayCount{0};
    std::size_t ActiveTriangleCount{0};
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
    // ErrorEvaluationCount 记录批量评分覆盖的 active leaf 数量
    std::size_t ErrorEvaluationCount{0};
    // worker count 是本帧实际采用的并行宽度
    std::size_t ErrorEvaluationWorkerCount{0};
    // collect 和 mark 分开统计，便于观察不规则拓扑扫描成本
    std::size_t CollectWorkerCount{0};
    std::size_t CandidateMarkWorkerCount{0};
    // emit worker 数用于确认 CPU mesh 输出是否进入并行路径
    std::size_t EmitWorkerCount{0};
    // 候选数量用于观察并行标记后的队列规模
    std::size_t SplitCandidateCount{0};
    std::size_t MergeCandidateCount{0};
    // terrain chunk 统计用于观察保守并发提交覆盖面
    std::size_t TopologyChunkCount{0};
    // commit worker 数只统计 topology commit batch
    std::size_t TopologyCommitWorkerCount{0};
    // interior candidate 满足同 chunk 写入约束
    std::size_t InteriorSplitCandidateCount{0};
    // boundary candidate 由串行回退处理
    std::size_t BoundarySplitCandidateCount{0};
    // merge interior 需要 diamond 影响节点都在同一 chunk
    std::size_t InteriorMergeCandidateCount{0};
    // 跨 chunk diamond merge 必须保持串行
    std::size_t BoundaryMergeCandidateCount{0};
    // 并发提交只统计真正修改的拓扑节点
    std::size_t ParallelSplitCommitCount{0};
    // parallel merge count 可用于观察 chunk commit 覆盖面
    std::size_t ParallelMergeCommitCount{0};
    float UpdateMilliseconds{0.0F};
    // 两个耗时字段按实际路径择一写入
    float ErrorEvaluationSingleThreadMilliseconds{0.0F};
    float ErrorEvaluationParallelMilliseconds{0.0F};
    // collect/mark 耗时会汇总到统一 CpuCollectMilliseconds
    float ActiveLeafCollectMilliseconds{0.0F};
    float SplitCandidateMarkMilliseconds{0.0F};
    float MergeCandidateMarkMilliseconds{0.0F};
    float SplitMilliseconds{0.0F};
    float EmitMilliseconds{0.0F};
    float ValidateMilliseconds{0.0F};
    float MergeMilliseconds{0.0F};
    int MaxDepthReached{0};
};

/// <summary>
/// Data-Oriented CPU ROAM 在 SoA 节点池上评估 active leaf screen error
/// </summary>
class DataOrientedRoamMeshBuilder
{
public:
    DataOrientedRoamMeshBuilder();
    ~DataOrientedRoamMeshBuilder();

    DataOrientedRoamMeshBuilder(const DataOrientedRoamMeshBuilder&) = delete;
    DataOrientedRoamMeshBuilder& operator=(const DataOrientedRoamMeshBuilder&) = delete;
    DataOrientedRoamMeshBuilder(DataOrientedRoamMeshBuilder&&) noexcept;
    DataOrientedRoamMeshBuilder& operator=(DataOrientedRoamMeshBuilder&&) noexcept;

    [[nodiscard]] Terrain::TerrainMeshData Build(
        const Terrain::HeightMap& heightMap,
        float terrainSize,
        float heightScale,
        const glm::vec3& cameraPosition,
        const DataOrientedRoamSettings& settings);

    void UpdateTopology(
        const Terrain::HeightMap& heightMap,
        float terrainSize,
        float heightScale,
        const glm::vec3& cameraPosition,
        const DataOrientedRoamSettings& settings);

    [[nodiscard]] const DataOrientedRoamStats& Stats() const;
    [[nodiscard]] const DataOrientedRoamState& State() const;

private:
    [[nodiscard]] Terrain::TerrainMeshData BuildInternal(
        const Terrain::HeightMap& heightMap,
        float terrainSize,
        float heightScale,
        const glm::vec3& cameraPosition,
        const DataOrientedRoamSettings& settings,
        bool emitCpuMesh);

    std::unique_ptr<DataOrientedRoamState> _state;
    std::unique_ptr<DataOrientedRoamThreadPool> _threadPool;
};
} // 命名空间 ParallelRoam::Algorithms::DataOrientedRoam
