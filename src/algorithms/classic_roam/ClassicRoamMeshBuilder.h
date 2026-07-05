#pragma once

#include "terrain/HeightMap.h"
#include "terrain/TerrainMeshBuilder.h"

#include <glm/glm.hpp>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <unordered_set>
#include <vector>

namespace ParallelRoam::Algorithms::ClassicRoam
{
/// <summary>
/// 使用 UV 空间表达一个 ROAM 三角形域，实际顶点在 mesh 输出时采样高度图生成
/// </summary>
struct TriangleDomain
{
    // 三个 UV 点保持逆时针绕序，最终会映射到 XZ 平面
    glm::vec2 A{0.0F};
    glm::vec2 B{0.0F};
    glm::vec2 C{0.0F};
};

/// <summary>
/// Classic CPU ROAM 的单帧细分、合并和拓扑验证参数
/// </summary>
struct ClassicRoamSettings
{
    // MaxDepth 控制 bintree 最细层级，129 高度图需要 14 才接近规则网格间距
    int MaxDepth{14};

    // 简化 screen-space error 超过该阈值时触发 split
    float SplitThreshold{0.04F};

    // 简化 screen-space error 低于该阈值时允许 merge
    float MergeThreshold{0.02F};

    // 距离权重越大，远处也会更积极细分
    float DistanceScale{24.0F};

    // SplitBudget 限制单次 build 的 split 次数，避免交互路径一次展开过多节点
    std::size_t SplitBudget{8192};

    // 开启后会基于 baseNeighbor 执行局部 diamond forced split
    bool EnableLocalConstraints{true};

    // 拓扑验证会做全局扫描，只用于 debug，不参与默认修复路径
    bool EnableTopologyValidation{false};
};

/// <summary>
/// Classic CPU ROAM 的运行统计，记录拓扑规模、split/merge 行为和各 pass 耗时
/// </summary>
struct ClassicRoamStats
{
    // NodeCount 包含内部节点和 leaf 节点
    std::size_t NodeCount{0};

    // ActiveTriangleCount 只统计当前用于渲染的 leaf
    std::size_t ActiveTriangleCount{0};

    // OriginalTriangleCount 统计未细分的原始 leaf triangle
    std::size_t OriginalTriangleCount{0};

    // SubdividedTriangleCount 统计稳定存在的细分 leaf triangle
    std::size_t SubdividedTriangleCount{0};

    // RebuiltTriangleCount 统计本次 build 中新激活或 merge 回来的 leaf triangle
    std::size_t RebuiltTriangleCount{0};

    // ActiveSplitCount 统计当前仍处于展开状态的 internal triangle
    std::size_t ActiveSplitCount{0};

    // SplitCount 统计本次 build 中实际发生的 split 次数
    std::size_t SplitCount{0};

    // ForcedSplitCount 统计裂缝约束额外触发的 split
    std::size_t ForcedSplitCount{0};

    // MergeCount 统计上一帧 split 但本帧回落为 merge 的节点
    std::size_t MergeCount{0};

    // CrackRiskCount 统计达到最大深度后仍无法继续修复的裂缝风险
    std::size_t CrackRiskCount{0};

    // ConstraintPassCount 统计 baseNeighbor 约束传播次数
    std::size_t ConstraintPassCount{0};

    // CandidatePeakCount 记录 split queue 峰值，观察候选队列压力
    std::size_t CandidatePeakCount{0};

    // RejectedSplitCount 表示因预算或深度上限被拒绝的 split
    std::size_t RejectedSplitCount{0};

    // RejectedMergeCount 表示 diamond 条件不满足而拒绝 merge
    std::size_t RejectedMergeCount{0};

    // TjunctionCount 只由 validator 统计，默认不参与每帧修复
    std::size_t TjunctionCount{0};

    // InvalidNeighborCount 表示 validator 发现的邻接互反或共享边错误
    std::size_t InvalidNeighborCount{0};

    // InvalidTopologyCount 表示 parent / child / root diamond 不变量错误
    std::size_t InvalidTopologyCount{0};

    // UpdateMilliseconds 覆盖本次 Build 的完整 CPU 时间
    float UpdateMilliseconds{0.0F};

    // SplitMilliseconds 只统计候选队列和拓扑 split 时间
    float SplitMilliseconds{0.0F};

    // EmitMilliseconds 只统计 leaf mesh 输出时间
    float EmitMilliseconds{0.0F};

    // ValidateMilliseconds 只在拓扑验证开启时有意义
    float ValidateMilliseconds{0.0F};

    // MergeMilliseconds 只统计 diamond merge candidate 和拓扑回收时间
    float MergeMilliseconds{0.0F};

    // MaxDepthReached 用于验证 split 是否按预期展开
    int MaxDepthReached{0};
};

/// <summary>
/// Classic CPU ROAM 的裸指针二叉三角树网格生成器
/// </summary>
class ClassicRoamMeshBuilder
{
public:
    /// <summary>
    /// 根据相机位置和误差阈值生成当前 active leaf triangle mesh
    /// </summary>
    [[nodiscard]] Terrain::TerrainMeshData Build(
        const Terrain::HeightMap& heightMap,
        float terrainSize,
        float heightScale,
        const glm::vec3& cameraPosition,
        const ClassicRoamSettings& settings);

    [[nodiscard]] const ClassicRoamStats& Stats() const;

private:
    enum class SplitReason
    {
        // 普通误差阈值触发的 split
        Requested,

        // baseNeighbor 为了补齐 diamond 触发的 split
        ForcedByBaseNeighbor,
    };

    /// <summary>
    /// Classic ROAM 的持久化二叉三角树节点，使用裸指针表达 parent / child / neighbor 拓扑
    /// </summary>
    struct ClassicRoamNode
    {
        // Domain 使用 UV 空间表达，避免节点保存重复三维顶点
        TriangleDomain Domain;

        // Parent 和 child 使用经典 ROAM 裸指针拓扑
        ClassicRoamNode* Parent{nullptr};
        ClassicRoamNode* LeftChild{nullptr};
        ClassicRoamNode* RightChild{nullptr};

        // 三个 neighbor 指针对应 base edge、left edge 和 right edge
        ClassicRoamNode* BaseNeighbor{nullptr};
        ClassicRoamNode* LeftNeighbor{nullptr};
        ClassicRoamNode* RightNeighbor{nullptr};

        // GeometricError 是当前节点边中点和重心的最大高度误差
        float GeometricError{0.0F};
        std::uint64_t PathId{0};
        std::uint64_t CreatedBuildId{0};
        std::uint64_t ActivatedBuildId{0};
        std::uint64_t SplitBuildId{0};
        std::uint64_t MergeBuildId{0};
        int Depth{0};
        bool ActivatedByForcedSplit{false};

        // IsSplit 决定 child 当前是否参与 active topology
        bool IsSplit{false};
    };

    [[nodiscard]] ClassicRoamNode* AddNode(
        const TriangleDomain& domain,
        ClassicRoamNode* parent,
        int depth,
        std::uint64_t pathId);

    // 初始化或重置持久化根 diamond
    void ResetTopology();

    // 判断设置变化是否必须重建整棵树
    [[nodiscard]] bool NeedsTopologyReset(
        const Terrain::HeightMap& heightMap,
        float terrainSize,
        float heightScale,
        const ClassicRoamSettings& settings) const;

    // 递归执行 split 决策，保留 Classic ROAM 的二叉三角树语义
    void RefineNode(ClassicRoamNode* node);

    // 使用 priority queue 处理 split candidate，避免纯递归一次展开过深
    void RefineWithSplitQueue(ClassicRoamNode* rootA, ClassicRoamNode* rootB);

    // 执行符合 diamond 约束的 merge
    void MergeWithDiamondQueue();

    // 沿 base edge split，生成两个 child triangle
    [[nodiscard]] bool SplitNode(
        ClassicRoamNode* node,
        SplitReason reason,
        ClassicRoamNode* forcedFrom);

    // split 后按 Classic ROAM diamond 关系连接 child 和 neighbor
    void LinkSplitNeighbors(ClassicRoamNode* node, ClassicRoamNode* baseNeighbor);

    // 邻居还指向旧 leaf 时，需要改指向 split 后对应的 child
    void ReplaceNeighborReference(ClassicRoamNode* neighbor, ClassicRoamNode* oldNode, ClassicRoamNode* newNode) const;

    // 判断 parent 是否可以安全回收为 leaf
    [[nodiscard]] bool CanMergeNode(const ClassicRoamNode* node) const;

    // 回收一个 parent 的两个 leaf child
    void MergeSingleNode(ClassicRoamNode* node);

    // 若 base neighbor 也 split，则按 diamond 成对回收
    [[nodiscard]] bool MergeNodeOrDiamond(ClassicRoamNode* node);

    // 收集当前 active leaf，供裂缝检测和 neighbor 重建复用
    void CollectLeafNodes(std::vector<ClassicRoamNode*>& leafNodes) const;

    // 从指定根节点收集 active leaf
    void CollectLeafNodesFrom(ClassicRoamNode* node, std::vector<ClassicRoamNode*>& leafNodes) const;

    // 收集当前 active internal path，供 hysteresis 复用
    void CollectActiveSplitPaths();
    void CollectActiveSplitPathsFrom(const ClassicRoamNode* node);

    // 聚合当前帧 leaf 分类和深度统计
    void AccumulateLeafStats(
        const Terrain::TerrainMeshData& meshData,
        const std::vector<ClassicRoamNode*>& leafNodes);

    // validator 只检查当前拓扑，不在默认路径修复裂缝
    void ValidateTopology();

    // 遍历 leaf 节点并输出渲染用 mesh
    void EmitLeafTriangles(
        Terrain::TerrainMeshData& meshData,
        const std::vector<ClassicRoamNode*>& leafNodes) const;
    void EmitNode(const ClassicRoamNode& node, Terrain::TerrainMeshData& meshData) const;
    void EmitDomainTriangle(const ClassicRoamNode& node, Terrain::TerrainMeshData& meshData) const;

    // 当前实现使用阈值决策，后续可替换为 priority queue
    [[nodiscard]] bool ShouldSplit(const ClassicRoamNode& node) const;
    [[nodiscard]] bool ShouldSplitWithScore(const ClassicRoamNode& node, float screenErrorScore) const;

    // 判断节点是否在 hysteresis 区间内沿用上一帧 split 状态
    [[nodiscard]] bool WasSplitLastFrame(const ClassicRoamNode& node) const;

    enum class LeafDebugClass
    {
        Original,
        Subdivided,
        Rebuilt,
    };

    // 对 active leaf 做调试分类，供颜色输出和 benchmark 统计共用
    [[nodiscard]] LeafDebugClass ClassifyLeafDebug(const ClassicRoamNode& node) const;

    // 按 leaf 调试分类输出稳定颜色，避免 UI 和 benchmark 口径分裂
    [[nodiscard]] glm::vec3 DebugColorForLeaf(const ClassicRoamNode& node) const;
    [[nodiscard]] float DebugHighlightForLeaf(const ClassicRoamNode& node) const;

    // 用边中点和重心高度差估算该三角形的几何误差
    [[nodiscard]] float ComputeGeometricError(const TriangleDomain& domain) const;

    // 简化后的 screen-space error，足够展示近细远粗
    [[nodiscard]] float ComputeScreenErrorScore(const ClassicRoamNode& node) const;

    // UV 到世界坐标的映射必须和规则网格 baseline 保持一致
    [[nodiscard]] glm::vec3 DomainToWorld(const glm::vec2& uv) const;

    // 使用 Height Map 邻域高度估算法线，避免 leaf 顶点重复导致硬边过重
    [[nodiscard]] glm::vec3 SampleNormal(const glm::vec2& uv) const;

    [[nodiscard]] bool IsLeaf(const ClassicRoamNode* node) const;

    const Terrain::HeightMap* _heightMap{nullptr};
    ClassicRoamSettings _settings;
    ClassicRoamStats _stats;

    // _nodes 只负责生命周期，算法拓扑通过 ClassicRoamNode* 表达
    std::vector<std::unique_ptr<ClassicRoamNode>> _nodes;
    std::unordered_set<std::uint64_t> _previousSplitPaths;
    std::unordered_set<std::uint64_t> _currentSplitPaths;
    // _activeLeaves 是拓扑稳定后的最终 leaf 快照，emit 和 stats 共用
    std::vector<ClassicRoamNode*> _activeLeaves;
    ClassicRoamNode* _rootA{nullptr};
    ClassicRoamNode* _rootB{nullptr};
    glm::vec3 _cameraPosition{0.0F};
    float _terrainSize{1.0F};
    float _heightScale{1.0F};
    int _topologyMaxDepth{0};
    std::uint64_t _buildSequence{0};
};
} // 命名空间 ParallelRoam::Algorithms::ClassicRoam
