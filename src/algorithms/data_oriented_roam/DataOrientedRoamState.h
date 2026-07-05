#pragma once

#include "algorithms/data_oriented_roam/DataOrientedRoamMeshBuilder.h"

#include <chrono>
#include <cstdint>
#include <limits>
#include <unordered_set>
#include <vector>

namespace ParallelRoam::Algorithms::DataOrientedRoam
{
using DataOrientedRoamNodeIndex = std::uint32_t;
constexpr DataOrientedRoamNodeIndex InvalidDataOrientedRoamNodeIndex =
    std::numeric_limits<DataOrientedRoamNodeIndex>::max();
// chunk id 是并发 topology commit 的 ownership 键
using DataOrientedRoamChunkId = std::uint32_t;
constexpr DataOrientedRoamChunkId InvalidDataOrientedRoamChunkId =
    std::numeric_limits<DataOrientedRoamChunkId>::max();
// 固定分块数避免把内部调度策略暴露成 UI 参数
constexpr int DataOrientedRoamTopologyChunkGridSize = 8;

enum class DataOrientedRoamSplitReason
{
    Requested,
    ForcedByBaseNeighbor,
};

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
    // Score 来自批量 ScreenErrors 缓存
    float Score{0.0F};
    // Sequence 只用于同分候选的稳定排序
    std::uint64_t Sequence{0};
    DataOrientedRoamNodeIndex Node{InvalidDataOrientedRoamNodeIndex};
};

/// <summary>
/// merge 队列的候选快照，拓扑提交前不会修改节点关系
/// </summary>
struct DataOrientedRoamMergeCandidate
{
    // merge 队列按低误差优先回收
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
    const std::uint64_t& PathId;
    const std::uint64_t& CreatedBuildId;
    const std::uint64_t& ActivatedBuildId;
    const std::uint64_t& SplitBuildId;
    const std::uint64_t& MergeBuildId;
    const int& Depth;
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
    std::uint64_t& PathId;
    std::uint64_t& CreatedBuildId;
    std::uint64_t& ActivatedBuildId;
    std::uint64_t& SplitBuildId;
    std::uint64_t& MergeBuildId;
    int& Depth;
    std::uint8_t& ActivatedByForcedSplit;
    std::uint8_t& IsSplit;

    [[nodiscard]] operator DataOrientedRoamNodeConstRef() const;
};

/// <summary>
/// Data-Oriented ROAM 的 SoA 节点池，拓扑、误差、深度和 flag 分别连续存储
/// </summary>
struct DataOrientedRoamNodePool
{
    // Domain 是节点的 UV 空间三角形，不保存冗余世界坐标
    std::vector<TriangleDomain> Domains;
    // Parent 用于 validator 检查持久化 node pool 的树关系
    std::vector<DataOrientedRoamNodeIndex> Parents;
    // child index 在 merge 后保留，下一次 split 可复用误差缓存
    std::vector<DataOrientedRoamNodeIndex> LeftChildren;
    // RightChildren 与 LeftChildren 保持同下标写入
    std::vector<DataOrientedRoamNodeIndex> RightChildren;
    // 三个 neighbor 对应 base、left、right 三条边
    std::vector<DataOrientedRoamNodeIndex> BaseNeighbors;
    std::vector<DataOrientedRoamNodeIndex> LeftNeighbors;
    // RightNeighbors 让边向邻接关系不需要临时对象
    std::vector<DataOrientedRoamNodeIndex> RightNeighbors;
    // InteriorChunkIds 缓存分块归属，避免 topology pass 反复按 UV 计算
    std::vector<DataOrientedRoamChunkId> InteriorChunkIds;
    // GeometricErrors 与相机无关，节点创建后跨帧复用
    std::vector<float> GeometricErrors;
    // ScreenErrors 缓存最近一次队列评分，供误差评估批量复用
    std::vector<float> ScreenErrors;
    // PathIds 是 hysteresis 的稳定键，不能使用 vector index 代替
    std::vector<std::uint64_t> PathIds;
    // build id 让 debug overlay 区分新建、激活和合并节点
    std::vector<std::uint64_t> CreatedBuildIds;
    std::vector<std::uint64_t> ActivatedBuildIds;
    // SplitBuildIds 和 MergeBuildIds 只服务本帧 debug 分类
    std::vector<std::uint64_t> SplitBuildIds;
    std::vector<std::uint64_t> MergeBuildIds;
    // Depths 直接参与 maxDepth 限制和 debug color 渐变
    std::vector<int> Depths;
    // flags 分离保存，避免和 index / float 字段混在同一 cache line
    std::vector<std::uint8_t> ActivatedByForcedSplits;
    std::vector<std::uint8_t> IsSplits;

    [[nodiscard]] std::size_t size() const;
    [[nodiscard]] std::size_t capacity() const;
    [[nodiscard]] std::size_t storage_bytes() const;
    // array_count 用于报告当前 SoA 字段拆分规模
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
        float geometricError);

    // operator[] 返回 proxy，避免 pass 直接依赖具体数组名
    [[nodiscard]] DataOrientedRoamNodeRef operator[](DataOrientedRoamNodeIndex node);
    [[nodiscard]] DataOrientedRoamNodeConstRef operator[](DataOrientedRoamNodeIndex node) const;
};

/// <summary>
/// DOD ROAM 的可变工作集，所有 pass 都只通过这个状态对象交换数据
/// </summary>
struct DataOrientedRoamState
{
    // HeightMap 不归 state 所有，Build 调用期间必须保持有效
    const Terrain::HeightMap* HeightMap{nullptr};
    // Settings 是本帧快照，pass 不读取外部 UI 状态
    DataOrientedRoamSettings Settings;
    // Stats 由各 pass 累积，builder 只负责更新时间桶
    DataOrientedRoamStats Stats;
    // Nodes 是 SoA node pool
    DataOrientedRoamNodePool Nodes;
    // PreviousSplitPaths 是 hysteresis 的跨帧记忆
    std::unordered_set<std::uint64_t> PreviousSplitPaths;
    // CurrentSplitPaths 在 merge/split 完成后重新收集
    std::unordered_set<std::uint64_t> CurrentSplitPaths;
    // FinalActiveLeaves 是拓扑稳定后的 leaf 快照，emit 和统计共用
    std::vector<DataOrientedRoamNodeIndex> FinalActiveLeaves;
    // RootA 和 RootB 构成初始 diamond
    DataOrientedRoamNodeIndex RootA{InvalidDataOrientedRoamNodeIndex};
    DataOrientedRoamNodeIndex RootB{InvalidDataOrientedRoamNodeIndex};
    // CameraPosition 只影响 screen error，不影响 geometric error 缓存
    glm::vec3 CameraPosition{0.0F};
    // TerrainSize 和 HeightScale 改变时需要保守重建拓扑
    float TerrainSize{1.0F};
    float HeightScale{1.0F};
    // TopologyMaxDepth 用于判断降低 maxDepth 时是否必须重置
    int TopologyMaxDepth{0};
    // BuildSequence 为当前帧拓扑变化打时间戳
    std::uint64_t BuildSequence{0};

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
    std::uint64_t pathId);

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

// CollectLeafNodes 只遍历 active topology，不返回 inactive child
void CollectLeafNodes(const DataOrientedRoamState& state, std::vector<DataOrientedRoamNodeIndex>& leafNodes);

// CollectActiveSplitPaths 在 merge/split 后更新 hysteresis 输入
void CollectActiveSplitPaths(DataOrientedRoamState& state);

// AccumulateLeafStats 聚合当前帧 active leaf 的 debug 分类
void AccumulateLeafStats(
    DataOrientedRoamState& state,
    const Terrain::TerrainMeshData& meshData,
    const std::vector<DataOrientedRoamNodeIndex>& leafNodes);

// RefineWithSplitQueue 是 split pass，受 SplitBudget 限制
void RefineWithSplitQueue(DataOrientedRoamState& state);

// MergeWithDiamondQueue 是 merge pass，先于 split pass 运行
void MergeWithDiamondQueue(DataOrientedRoamState& state);

// ValidateTopology 是可选 debug pass，不主动修复拓扑
void ValidateTopology(DataOrientedRoamState& state);

// EmitLeafTriangles 是当前 DOD 路径的 CPU mesh 输出 pass
void EmitLeafTriangles(
    DataOrientedRoamState& state,
    Terrain::TerrainMeshData& meshData,
    const std::vector<DataOrientedRoamNodeIndex>& leafNodes);

// EvaluateScreenErrorForNode 写回单个节点的 screen error 缓存
[[nodiscard]] float EvaluateScreenErrorForNode(DataOrientedRoamState& state, DataOrientedRoamNodeIndex node);

// EvaluateScreenErrors 批量刷新 active leaf 的 screen error 缓存
void EvaluateScreenErrors(DataOrientedRoamState& state, const std::vector<DataOrientedRoamNodeIndex>& leafNodes);

// CollectSplitCandidates 并行收集 active leaf 并标记 split 候选
void CollectSplitCandidates(DataOrientedRoamState& state, std::vector<DataOrientedRoamSplitCandidate>& candidates);

// CollectMergeCandidates 并行扫描 active internal node 并标记 merge 候选
void CollectMergeCandidates(DataOrientedRoamState& state, std::vector<DataOrientedRoamMergeCandidate>& candidates);

// CanMergeNode 只检查 diamond merge 前置条件，不修改拓扑
[[nodiscard]] bool CanMergeNode(const DataOrientedRoamState& state, DataOrientedRoamNodeIndex node);

// ShouldSplitWithScore 汇总 split 阈值、merge 阈值和 hysteresis 规则
[[nodiscard]] bool ShouldSplitWithScore(
    const DataOrientedRoamState& state,
    DataOrientedRoamNodeConstRef node,
    float screenErrorScore);

// WasSplitLastFrame 只读取上一帧最终 active split path
[[nodiscard]] bool WasSplitLastFrame(const DataOrientedRoamState& state, DataOrientedRoamNodeConstRef node);

// ClassifyLeafDebug 把节点生命周期映射成 UI 可视化分类
[[nodiscard]] DataOrientedRoamLeafDebugClass ClassifyLeafDebug(
    const DataOrientedRoamState& state,
    DataOrientedRoamNodeConstRef node);

// DebugColorForLeaf 和 DebugHighlightForLeaf 必须与 ImGui legend 语义一致
[[nodiscard]] glm::vec3 DebugColorForLeaf(const DataOrientedRoamState& state, DataOrientedRoamNodeConstRef node);
[[nodiscard]] float DebugHighlightForLeaf(const DataOrientedRoamState& state, DataOrientedRoamNodeConstRef node);

// ComputeGeometricError 只依赖 HeightMap 与 TriangleDomain
[[nodiscard]] float ComputeGeometricError(const DataOrientedRoamState& state, const TriangleDomain& domain);

// ComputeScreenErrorScore 是当前 split/merge 队列排序的统一评分
[[nodiscard]] float ComputeScreenErrorScore(const DataOrientedRoamState& state, DataOrientedRoamNodeConstRef node);

// DomainToWorld 保持与规则网格 builder 相同的世界坐标约定
[[nodiscard]] glm::vec3 DomainToWorld(const DataOrientedRoamState& state, const glm::vec2& uv);

// SampleNormal 从 HeightMap 梯度估计，避免依赖 leaf 邻接关系
[[nodiscard]] glm::vec3 SampleNormal(const DataOrientedRoamState& state, const glm::vec2& uv);
} // 命名空间 ParallelRoam::Algorithms::DataOrientedRoam
