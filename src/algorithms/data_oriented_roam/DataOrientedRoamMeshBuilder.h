#pragma once

#include "terrain/HeightMap.h"
#include "terrain/TerrainMeshBuilder.h"

#include <glm/glm.hpp>

#include <memory>
#include <cstddef>

namespace ParallelRoam::Algorithms::DataOrientedRoam
{
struct DataOrientedRoamState;

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
/// Data-Oriented CPU ROAM 3B 的单帧细分、合并和拓扑验证参数
/// </summary>
struct DataOrientedRoamSettings
{
    int MaxDepth{14};
    float SplitThreshold{0.04F};
    float MergeThreshold{0.02F};
    float DistanceScale{24.0F};
    std::size_t SplitBudget{8192};
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
    float UpdateMilliseconds{0.0F};
    float SplitMilliseconds{0.0F};
    float EmitMilliseconds{0.0F};
    float ValidateMilliseconds{0.0F};
    float MergeMilliseconds{0.0F};
    int MaxDepthReached{0};
};

/// <summary>
/// Data-Oriented CPU ROAM 的 3B 版本：节点池使用 SoA 数组，拓扑关系全部使用 index 表达
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

    [[nodiscard]] const DataOrientedRoamStats& Stats() const;

private:
    std::unique_ptr<DataOrientedRoamState> _state;
};
} // 命名空间 ParallelRoam::Algorithms::DataOrientedRoam
