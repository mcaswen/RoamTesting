#pragma once

#include "terrain/HeightMap.h"
#include "terrain/TerrainMeshBuilder.h"

#include <glm/glm.hpp>

#include <cstddef>
#include <cstdint>
#include <limits>
#include <unordered_set>
#include <vector>

namespace ParallelRoam::Algorithms::DataOrientedRoam
{
/// <summary>
/// 使用 UV 空间表达一个 ROAM 三角形域，避免节点池重复保存世界空间顶点。
/// </summary>
struct TriangleDomain
{
    glm::vec2 A{0.0F};
    glm::vec2 B{0.0F};
    glm::vec2 C{0.0F};
};

/// <summary>
/// Data-Oriented CPU ROAM 3A 的单帧细分、合并和拓扑验证参数。
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
/// Data-Oriented CPU ROAM 的运行统计，补充节点池容量以观察预分配效果。
/// </summary>
struct DataOrientedRoamStats
{
    std::size_t NodeCount{0};
    std::size_t ReservedNodeCapacity{0};
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
/// Data-Oriented CPU ROAM 的 3A 版本：节点由预分配 vector 池管理，拓扑关系全部使用 index 表达
/// </summary>
class DataOrientedRoamMeshBuilder
{
public:
    [[nodiscard]] Terrain::TerrainMeshData Build(
        const Terrain::HeightMap& heightMap,
        float terrainSize,
        float heightScale,
        const glm::vec3& cameraPosition,
        const DataOrientedRoamSettings& settings);

    [[nodiscard]] const DataOrientedRoamStats& Stats() const;

private:
    using NodeIndex = std::uint32_t;
    static constexpr NodeIndex InvalidNodeIndex = std::numeric_limits<NodeIndex>::max();

    enum class SplitReason
    {
        Requested,
        ForcedByBaseNeighbor,
    };

    /// <summary>
    /// 3A 的 AoS 节点记录，所有拓扑关系都保存为 NodeIndex，后续 3B 会拆分为 SoA 数组。
    /// </summary>
    struct DataOrientedRoamNode
    {
        TriangleDomain Domain;
        NodeIndex Parent{InvalidNodeIndex};
        NodeIndex LeftChild{InvalidNodeIndex};
        NodeIndex RightChild{InvalidNodeIndex};
        NodeIndex BaseNeighbor{InvalidNodeIndex};
        NodeIndex LeftNeighbor{InvalidNodeIndex};
        NodeIndex RightNeighbor{InvalidNodeIndex};
        float GeometricError{0.0F};
        std::uint64_t PathId{0};
        std::uint64_t CreatedBuildId{0};
        std::uint64_t ActivatedBuildId{0};
        std::uint64_t SplitBuildId{0};
        std::uint64_t MergeBuildId{0};
        int Depth{0};
        bool ActivatedByForcedSplit{false};
        bool IsSplit{false};
    };

    [[nodiscard]] NodeIndex AddNode(
        const TriangleDomain& domain,
        NodeIndex parent,
        int depth,
        std::uint64_t pathId);

    void ReserveNodePool();
    void ResetTopology();

    [[nodiscard]] bool NeedsTopologyReset(
        const Terrain::HeightMap& heightMap,
        float terrainSize,
        float heightScale,
        const DataOrientedRoamSettings& settings) const;

    void RefineWithSplitQueue(NodeIndex rootA, NodeIndex rootB);
    void MergeWithDiamondQueue();

    [[nodiscard]] bool SplitNode(
        NodeIndex node,
        SplitReason reason,
        NodeIndex forcedFrom);

    void LinkSplitNeighbors(NodeIndex node, NodeIndex baseNeighbor);
    void ReplaceNeighborReference(NodeIndex neighbor, NodeIndex oldNode, NodeIndex newNode);

    [[nodiscard]] bool CanMergeNode(NodeIndex node) const;
    void MergeSingleNode(NodeIndex node);
    [[nodiscard]] bool MergeNodeOrDiamond(NodeIndex node);

    void CollectLeafNodes(std::vector<NodeIndex>& leafNodes) const;
    void CollectLeafNodesFrom(NodeIndex node, std::vector<NodeIndex>& leafNodes) const;
    void CollectActiveSplitPaths();
    void CollectActiveSplitPathsFrom(NodeIndex node);

    void ValidateTopology();

    void EmitLeafTriangles(Terrain::TerrainMeshData& meshData) const;
    void EmitNode(NodeIndex node, Terrain::TerrainMeshData& meshData) const;
    void EmitDomainTriangle(const DataOrientedRoamNode& node, Terrain::TerrainMeshData& meshData) const;

    [[nodiscard]] bool ShouldSplitWithScore(const DataOrientedRoamNode& node, float screenErrorScore) const;
    [[nodiscard]] bool WasSplitLastFrame(const DataOrientedRoamNode& node) const;

    enum class LeafDebugClass
    {
        Original,
        Subdivided,
        Rebuilt,
    };

    [[nodiscard]] LeafDebugClass ClassifyLeafDebug(const DataOrientedRoamNode& node) const;
    [[nodiscard]] glm::vec3 DebugColorForLeaf(const DataOrientedRoamNode& node) const;
    [[nodiscard]] float DebugHighlightForLeaf(const DataOrientedRoamNode& node) const;

    [[nodiscard]] float ComputeGeometricError(const TriangleDomain& domain) const;
    [[nodiscard]] float ComputeScreenErrorScore(const DataOrientedRoamNode& node) const;
    [[nodiscard]] glm::vec3 DomainToWorld(const glm::vec2& uv) const;
    [[nodiscard]] glm::vec3 SampleNormal(const glm::vec2& uv) const;

    [[nodiscard]] bool IsValidNode(NodeIndex node) const;
    [[nodiscard]] bool IsLeaf(NodeIndex node) const;

    const Terrain::HeightMap* _heightMap{nullptr};
    DataOrientedRoamSettings _settings;
    DataOrientedRoamStats _stats;
    std::vector<DataOrientedRoamNode> _nodes;
    std::unordered_set<std::uint64_t> _previousSplitPaths;
    std::unordered_set<std::uint64_t> _currentSplitPaths;
    NodeIndex _rootA{InvalidNodeIndex};
    NodeIndex _rootB{InvalidNodeIndex};
    glm::vec3 _cameraPosition{0.0F};
    float _terrainSize{1.0F};
    float _heightScale{1.0F};
    int _topologyMaxDepth{0};
    std::uint64_t _buildSequence{0};
};
} // 命名空间 ParallelRoam::Algorithms::DataOrientedRoam
