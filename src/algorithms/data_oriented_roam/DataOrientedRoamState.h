#pragma once

#include "algorithms/data_oriented_roam/DataOrientedRoamMeshBuilder.h"

#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <limits>
#include <unordered_set>
#include <vector>

namespace ParallelRoam::Algorithms::DataOrientedRoam
{
class DataOrientedRoamThreadPool;

using DataOrientedRoamNodeIndex = std::uint32_t;
constexpr DataOrientedRoamNodeIndex InvalidDataOrientedRoamNodeIndex =
    std::numeric_limits<DataOrientedRoamNodeIndex>::max();
constexpr std::size_t InvalidActiveNodePosition =
    std::numeric_limits<std::size_t>::max();
// chunk id 是并发 topology commit 的 ownership 键
using DataOrientedRoamChunkId = std::uint32_t;
constexpr DataOrientedRoamChunkId InvalidDataOrientedRoamChunkId =
    std::numeric_limits<DataOrientedRoamChunkId>::max();
// 固定分块数避免把内部调度策略暴露成 UI 参数
constexpr int DataOrientedRoamTopologyChunkGridSize = 8;

/// <summary>
/// split 提交的来源，用于区分误差驱动和兼容约束传播
/// </summary>
enum class DataOrientedRoamSplitReason
{
    Requested,
    ForcedByBaseNeighbor,
};

/// <summary>
/// 活动叶相对当前 build 的生命周期分类
/// </summary>
enum class DataOrientedRoamLeafDebugClass
{
    Original,
    Subdivided,
    Rebuilt,
};

/// <summary>
/// split priority queue 的候选快照，由候选标记 pass 生成
/// </summary>
struct DataOrientedRoamSplitCandidate
{
    // 高误差优先，Sequence 保证并行收集后的同分候选仍可确定排序
    float Score{0.0F};
    std::uint64_t Sequence{0};
    DataOrientedRoamNodeIndex Node{InvalidDataOrientedRoamNodeIndex};
};

/// <summary>
/// merge 队列的候选快照，拓扑提交前不会修改节点关系
/// </summary>
struct DataOrientedRoamMergeCandidate
{
    // 低误差 diamond 优先回收
    float Score{0.0F};
    DataOrientedRoamNodeIndex Node{InvalidDataOrientedRoamNodeIndex};
};

/// <summary>
/// SoA 节点池中单个节点的只读视图，字段引用到底层连续数组
/// </summary>
struct DataOrientedRoamNodeConstRef
{
    const TriangleDomain& Domain;
    const DataOrientedRoamNodeIndex& Parent;
    const DataOrientedRoamNodeIndex& LeftChild;
    const DataOrientedRoamNodeIndex& RightChild;
    const DataOrientedRoamNodeIndex& BaseNeighbor;
    const DataOrientedRoamNodeIndex& LeftNeighbor;
    const DataOrientedRoamNodeIndex& RightNeighbor;
    const DataOrientedRoamChunkId& InteriorChunkId;
    const float& GeometricError;
    const float& ScreenError;
    const std::size_t& VarianceIndex;
    const std::uint64_t& PathId;
    const std::uint64_t& CreatedBuildId;
    const std::uint64_t& ActivatedBuildId;
    const std::uint64_t& SplitBuildId;
    const std::uint64_t& MergeBuildId;
    const int& Depth;
    const std::uint8_t& VarianceTreeIndex;
    const std::uint8_t& ActivatedByForcedSplit;
    const std::uint8_t& IsSplit;
};

/// <summary>
/// SoA 节点池中单个节点的可写视图，算法 pass 通过它保持字段访问可读性
/// </summary>
struct DataOrientedRoamNodeRef
{
    TriangleDomain& Domain;
    DataOrientedRoamNodeIndex& Parent;
    DataOrientedRoamNodeIndex& LeftChild;
    DataOrientedRoamNodeIndex& RightChild;
    DataOrientedRoamNodeIndex& BaseNeighbor;
    DataOrientedRoamNodeIndex& LeftNeighbor;
    DataOrientedRoamNodeIndex& RightNeighbor;
    DataOrientedRoamChunkId& InteriorChunkId;
    float& GeometricError;
    float& ScreenError;
    std::size_t& VarianceIndex;
    std::uint64_t& PathId;
    std::uint64_t& CreatedBuildId;
    std::uint64_t& ActivatedBuildId;
    std::uint64_t& SplitBuildId;
    std::uint64_t& MergeBuildId;
    int& Depth;
    std::uint8_t& VarianceTreeIndex;
    std::uint8_t& ActivatedByForcedSplit;
    std::uint8_t& IsSplit;

    [[nodiscard]] operator DataOrientedRoamNodeConstRef() const;
};

/// <summary>
/// Data-Oriented ROAM 的 SoA 节点池，拓扑、误差、深度和 flag 分别连续存储
/// </summary>
struct DataOrientedRoamNodePool
{
    // 几何只保存 UV 定义域，世界坐标在评分和 emit 时按需恢复
    std::vector<TriangleDomain> Domains;
    std::vector<DataOrientedRoamNodeIndex> Parents;

    // merge 后保留 child index，使后续 split 可以复用节点和静态误差
    std::vector<DataOrientedRoamNodeIndex> LeftChildren;
    std::vector<DataOrientedRoamNodeIndex> RightChildren;

    // 三个 neighbor 对应 base、left、right 三条边
    std::vector<DataOrientedRoamNodeIndex> BaseNeighbors;
    std::vector<DataOrientedRoamNodeIndex> LeftNeighbors;
    std::vector<DataOrientedRoamNodeIndex> RightNeighbors;

    // InteriorChunkIds 缓存分块归属，避免 topology pass 反复按 UV 计算
    std::vector<DataOrientedRoamChunkId> InteriorChunkIds;

    // GeometricErrors 保存 nested wedgie thickness，与相机无关且可跨帧复用
    std::vector<float> GeometricErrors;
    std::vector<float> ScreenErrors;
    std::vector<std::size_t> VarianceIndices;

    // PathIds 是 hysteresis 的稳定键，不能使用 vector index 代替
    std::vector<std::uint64_t> PathIds;

    // build id 让 debug overlay 区分新建、激活和合并节点
    std::vector<std::uint64_t> CreatedBuildIds;
    std::vector<std::uint64_t> ActivatedBuildIds;
    std::vector<std::uint64_t> SplitBuildIds;
    std::vector<std::uint64_t> MergeBuildIds;
    std::vector<int> Depths;
    std::vector<std::uint8_t> VarianceTreeIndices;

    // flags 分离保存，避免和 index / float 字段混在同一 cache line
    std::vector<std::uint8_t> ActivatedByForcedSplits;
    std::vector<std::uint8_t> IsSplits;

    [[nodiscard]] std::size_t size() const;
    [[nodiscard]] std::size_t capacity() const;
    [[nodiscard]] std::size_t storage_bytes() const;
    [[nodiscard]] std::size_t array_count() const;
    [[nodiscard]] bool empty() const;

    void clear();
    void reserve(std::size_t capacity);

    [[nodiscard]] DataOrientedRoamNodeIndex Add(
        const TriangleDomain& domain,
        DataOrientedRoamNodeIndex parent,
        int depth,
        std::uint64_t pathId,
        std::uint64_t buildSequence,
        float geometricError,
        std::uint8_t varianceTreeIndex,
        std::size_t varianceIndex);

    // proxy 让 pass 保持节点语义，同时底层继续使用 SoA 布局
    [[nodiscard]] DataOrientedRoamNodeRef operator[](DataOrientedRoamNodeIndex node);
    [[nodiscard]] DataOrientedRoamNodeConstRef operator[](DataOrientedRoamNodeIndex node) const;
};

/// <summary>
/// DOD ROAM 的可变工作集，所有 pass 都只通过这个状态对象交换数据
/// </summary>
struct DataOrientedRoamState
{
    // HeightMap 和 ThreadPool 只在 Build 调用期间借用
    const Terrain::HeightMap* HeightMap{nullptr};
    DataOrientedRoamSettings Settings;
    DataOrientedRoamStats Stats;
    DataOrientedRoamNodePool Nodes;

    // 两棵 nested wedgie tree 分别对应两个根三角形；深度可超过运行时 MaxDepth
    std::array<std::vector<float>, 2> VarianceTrees;
    const Terrain::HeightMap* VarianceHeightMap{nullptr};
    int VarianceTreeMaxDepth{-1};

    // 两组稳定 path id 在帧边界交换，为 split/merge 提供 hysteresis 记忆
    std::unordered_set<std::uint64_t> PreviousSplitPaths;
    std::unordered_set<std::uint64_t> CurrentSplitPaths;

    // 当前活动 internal 节点的连续索引，避免 merge 每帧扫描历史 node pool
    std::vector<DataOrientedRoamNodeIndex> ActiveInternalNodes;
    // 节点索引到 ActiveInternalNodes 位置的反向表，支持 O(1) swap-remove
    std::vector<std::size_t> ActiveInternalNodePositions;
    // 当前 active leaf 的连续索引直接驱动融合 split scan。
    std::vector<DataOrientedRoamNodeIndex> ActiveLeafNodes;
    std::vector<std::size_t> ActiveLeafNodePositions;

    // RootA 和 RootB 构成初始 diamond
    DataOrientedRoamNodeIndex RootA{InvalidDataOrientedRoamNodeIndex};
    DataOrientedRoamNodeIndex RootB{InvalidDataOrientedRoamNodeIndex};

    glm::mat4 View{1.0F};
    glm::mat4 Projection{1.0F};
    std::array<glm::vec4, 6> FrustumPlanes{};
    std::uint32_t DrawableHeight{1U};
    // 并行 split commit 通过原子 token 共享同一硬预算
    std::atomic<std::size_t> RemainingSplitBudget{0U};
    float TerrainSize{1.0F};
    float HeightScale{1.0F};
    int TopologyMaxDepth{0};
    std::uint64_t BuildSequence{0};
    DataOrientedRoamThreadPool* ThreadPool{nullptr};

    [[nodiscard]] bool IsValidNode(DataOrientedRoamNodeIndex node) const;
    [[nodiscard]] bool IsLeaf(DataOrientedRoamNodeIndex node) const;
};

[[nodiscard]] float ElapsedMilliseconds(
    std::chrono::steady_clock::time_point start,
    std::chrono::steady_clock::time_point end);

[[nodiscard]] std::uint64_t LeftChildPathId(std::uint64_t parentPathId);
[[nodiscard]] std::uint64_t RightChildPathId(std::uint64_t parentPathId);

// ComputeInteriorChunkId 把完全落入同一分块的 domain 编码成 row-major id
[[nodiscard]] DataOrientedRoamChunkId ComputeInteriorChunkId(const TriangleDomain& domain);

// AddNode 是唯一写入 node pool 并计算 geometric error 的入口
[[nodiscard]] DataOrientedRoamNodeIndex AddNode(
    DataOrientedRoamState& state,
    const TriangleDomain& domain,
    DataOrientedRoamNodeIndex parent,
    int depth,
    std::uint64_t pathId,
    std::uint8_t varianceTreeIndex,
    std::size_t varianceIndex);

void RebuildVarianceTrees(DataOrientedRoamState& state, int finestDepth);
void RefreshNodeVarianceErrors(DataOrientedRoamState& state);
[[nodiscard]] float VarianceError(
    const DataOrientedRoamState& state,
    std::uint8_t varianceTreeIndex,
    std::size_t varianceIndex);

// ReserveNodePool 只优化扩容频率，算法正确性不能依赖地址稳定
void ReserveNodePool(DataOrientedRoamState& state);

// ResetTopology 只在输入资源或拓扑上限不兼容时调用
void ResetTopology(DataOrientedRoamState& state);

// NeedsTopologyReset 在本帧状态写入前比较旧 state 与新输入
[[nodiscard]] bool NeedsTopologyReset(
    const DataOrientedRoamState& state,
    const Terrain::HeightMap& heightMap,
    float terrainSize,
    float heightScale,
    const DataOrientedRoamSettings& settings);

// CollectLeafNodes 独立递归遍历 active topology，仅供 validator 交叉校验活动索引。
void CollectLeafNodes(const DataOrientedRoamState& state, std::vector<DataOrientedRoamNodeIndex>& leafNodes);

void CollectActiveSplitPaths(DataOrientedRoamState& state);

void AccumulateLeafStats(
    DataOrientedRoamState& state,
    const std::vector<DataOrientedRoamNodeIndex>& leafNodes);

// merge 先回收低误差 diamond，split 再按高误差顺序消费剩余预算
void RefineWithSplitQueue(DataOrientedRoamState& state);
void MergeWithDiamondQueue(DataOrientedRoamState& state);

// ValidateTopology 是可选 debug pass，不主动修复拓扑
void ValidateTopology(DataOrientedRoamState& state);

void EmitLeafTriangles(
    DataOrientedRoamState& state,
    Terrain::TerrainMeshData& meshData,
    const std::vector<DataOrientedRoamNodeIndex>& leafNodes);

[[nodiscard]] float EvaluateScreenErrorForNode(DataOrientedRoamState& state, DataOrientedRoamNodeIndex node);

void CollectSplitCandidates(DataOrientedRoamState& state, std::vector<DataOrientedRoamSplitCandidate>& candidates);

void CollectMergeCandidates(DataOrientedRoamState& state, std::vector<DataOrientedRoamMergeCandidate>& candidates);

void CollectMergeCandidates(
    DataOrientedRoamState& state,
    std::vector<DataOrientedRoamMergeCandidate>& candidates,
    float maximumScore);

// CanMergeNode 只检查 diamond merge 前置条件，不修改拓扑
[[nodiscard]] bool CanMergeNode(const DataOrientedRoamState& state, DataOrientedRoamNodeIndex node);

[[nodiscard]] bool CanMergeNode(
    const DataOrientedRoamState& state,
    DataOrientedRoamNodeIndex node,
    float maximumScore);

// ShouldSplitWithScore 汇总 split 阈值、merge 阈值和 hysteresis 规则
[[nodiscard]] bool ShouldSplitWithScore(
    const DataOrientedRoamState& state,
    DataOrientedRoamNodeConstRef node,
    float screenErrorScore);

// WasSplitLastFrame 只读取上一帧最终 active split path
[[nodiscard]] bool WasSplitLastFrame(const DataOrientedRoamState& state, DataOrientedRoamNodeConstRef node);

[[nodiscard]] DataOrientedRoamLeafDebugClass ClassifyLeafDebug(
    const DataOrientedRoamState& state,
    DataOrientedRoamNodeConstRef node);

// DebugColorForLeaf 和 DebugHighlightForLeaf 必须与 ImGui legend 语义一致
[[nodiscard]] glm::vec3 DebugColorForLeaf(const DataOrientedRoamState& state, DataOrientedRoamNodeConstRef node);
[[nodiscard]] float DebugHighlightForLeaf(const DataOrientedRoamState& state, DataOrientedRoamNodeConstRef node);

// 返回 base edge 中点相对父三角形线性插值的有符号高度位移
[[nodiscard]] float ComputeBaseMidpointDisplacement(const DataOrientedRoamState& state, const TriangleDomain& domain);

// ComputeScreenErrorScore 是当前 split/merge 队列排序的统一评分
[[nodiscard]] float ComputeScreenErrorScore(const DataOrientedRoamState& state, DataOrientedRoamNodeConstRef node);

// DomainToWorld 保持与规则网格 builder 相同的世界坐标约定
[[nodiscard]] glm::vec3 DomainToWorld(const DataOrientedRoamState& state, const glm::vec2& uv);

// SampleNormal 从 HeightMap 梯度估计，避免依赖 leaf 邻接关系
[[nodiscard]] glm::vec3 SampleNormal(const DataOrientedRoamState& state, const glm::vec2& uv);
} // namespace ParallelRoam::Algorithms::DataOrientedRoam
