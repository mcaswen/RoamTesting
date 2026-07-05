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
struct TriangleDomain
{
    // 三个 UV 点保持逆时针绕序，最终会映射到 XZ 平面
    glm::vec2 A{0.0F};
    glm::vec2 B{0.0F};
    glm::vec2 C{0.0F};
};

struct ClassicRoamSettings
{
    // 当前阶段只做 split-only，最大深度是主要的安全阀
    int MaxDepth{8};

    // 简化 screen-space error 超过该阈值时触发 split
    float SplitThreshold{0.16F};

    // 简化 screen-space error 低于该阈值时允许 merge
    float MergeThreshold{0.08F};

    // 距离权重越大，远处也会更积极细分
    float DistanceScale{24.0F};

    // 开启后会基于 baseNeighbor 执行 diamond forced split
    bool EnableCrackFix{true};
};

struct ClassicRoamStats
{
    // NodeCount 包含内部节点和 leaf 节点
    std::size_t NodeCount{0};

    // ActiveTriangleCount 只统计当前用于渲染的 leaf
    std::size_t ActiveTriangleCount{0};

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
    /// <param name="heightMap">高度图数据。</param>
    /// <param name="terrainSize">地形世界尺寸。</param>
    /// <param name="heightScale">高度缩放。</param>
    /// <param name="cameraPosition">相机世界坐标。</param>
    /// <param name="settings">ROAM split 配置。</param>
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

        // T-junction repair pass 主动触发的 split
        ForcedByCrackRepair,
    };

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
        int Depth{0};
    };

    [[nodiscard]] ClassicRoamNode* AddNode(
        const TriangleDomain& domain,
        ClassicRoamNode* parent,
        int depth,
        std::uint64_t pathId);

    // 递归执行 split 决策，保留 Classic ROAM 的二叉三角树语义
    void RefineNode(ClassicRoamNode* node);

    // 沿 base edge split，生成两个 child triangle
    [[nodiscard]] bool SplitNode(
        ClassicRoamNode* node,
        SplitReason reason,
        ClassicRoamNode* forcedFrom);

    // split 后按 Classic ROAM diamond 关系连接 child 和 neighbor
    void LinkSplitNeighbors(ClassicRoamNode* node, ClassicRoamNode* baseNeighbor);

    // 邻居还指向旧 leaf 时，需要改指向 split 后对应的 child
    void ReplaceNeighborReference(ClassicRoamNode* neighbor, ClassicRoamNode* oldNode, ClassicRoamNode* newNode) const;

    // 反复扫描 T-junction，并通过 diamond split 修复粗 leaf
    void RepairCracksWithDiamondSplits();

    // 找到被其他 leaf 顶点切开的粗 leaf 边
    [[nodiscard]] std::size_t FindCrackRepairCandidates(std::vector<ClassicRoamNode*>& forcedSplitNodes) const;

    // 收集当前 active leaf，供裂缝检测和 neighbor 重建复用
    void CollectLeafNodes(std::vector<ClassicRoamNode*>& leafNodes) const;

    // 构建后重建 leaf 级 neighbor 指针，便于调试经典拓扑关系
    void RebuildLeafNeighborLinks();

    // 遍历 leaf 节点并输出渲染用 mesh
    void EmitLeafTriangles(Terrain::TerrainMeshData& meshData) const;
    void EmitNode(const ClassicRoamNode& node, Terrain::TerrainMeshData& meshData) const;
    void EmitDomainTriangle(const TriangleDomain& domain, Terrain::TerrainMeshData& meshData) const;

    // 当前阶段使用阈值决策，后续可替换为 priority queue
    [[nodiscard]] bool ShouldSplit(const ClassicRoamNode& node) const;

    // 判断节点是否在 hysteresis 区间内沿用上一帧 split 状态
    [[nodiscard]] bool WasSplitLastFrame(const ClassicRoamNode& node) const;

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
    glm::vec3 _cameraPosition{0.0F};
    float _terrainSize{1.0F};
    float _heightScale{1.0F};
};
} // 命名空间 ParallelRoam::Algorithms::ClassicRoam
