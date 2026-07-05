#include "algorithms/classic_roam/ClassicRoamMeshBuilder.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <functional>
#include <limits>
#include <memory>
#include <numeric>
#include <queue>
#include <unordered_map>
#include <unordered_set>

namespace ParallelRoam::Algorithms::ClassicRoam
{
namespace
{
// Classic builder 是后续 DOD 和 GPU 版本的行为基线
// 它故意保留裸指针拓扑
// 这样 split、merge、diamond 约束可以用最直接的 ROAM 语义表达
// Data-Oriented 版必须在相同输入下复现这些行为
// benchmark 因此能把性能差异归因到数据布局和任务拆分
// 而不是算法语义漂移
// 本文件的复杂点主要有三类
// 第一类是持久化 bintree 生命周期
// child 被 merge 后不会销毁
// 后续重新 split 可以复用旧节点和 geometric error
// 第二类是 baseNeighbor 约束传播
// 单侧 split 会制造 T-junction
// 因此必须沿 base edge 追到合法 diamond
// 第三类是 hysteresis
// split 和 merge 阈值分离
// 上一帧 active split path 会影响中间区间决策
// 这些约束都需要保持稳定
// 否则 DOD 版很难判断差异来自优化还是行为错误
constexpr std::uint64_t RootAPathId = 1ULL;

// 第二棵根树放到高位区间，避免 rootA child path 与 rootB root 撞号
constexpr std::uint64_t RootBPathId = 1ULL << 32U;

struct SplitEdge
{
    // Start 和 End 是本次 split 的边，Apex 是该边对面的顶点
    glm::vec2 Start{0.0F};
    glm::vec2 End{0.0F};
    glm::vec2 Apex{0.0F};
};

// validator 不依赖 neighbor 指针直接判断裂缝
// 它先把 active leaf 的几何边收集出来
// 再用量化线段查找 T-junction 风险
struct DomainEdge
{
    // DomainEdge 用 UV 空间表达 leaf 边界
    glm::vec2 Start{0.0F};
    glm::vec2 End{0.0F};
};

struct QuantizedPoint
{
    long long X{0};
    long long Y{0};
};

struct QuantizedLineKey
{
    // Direction 描述归一化直线方向
    // Constant 描述直线相对原点的位置
    long long DirectionX{0};
    long long DirectionY{0};
    long long Constant{0};

    bool operator==(const QuantizedLineKey& other) const
    {
        return DirectionX == other.DirectionX &&
               DirectionY == other.DirectionY &&
               Constant == other.Constant;
    }
};

struct QuantizedLineKeyHash
{
    std::size_t operator()(const QuantizedLineKey& key) const
    {
        std::size_t seed = 1469598103934665603ULL;
        const auto mix = [&seed](long long value) {
            const std::size_t hashedValue = std::hash<long long>{}(value);
            seed ^= hashedValue + 0x9e3779b97f4a7c15ULL + (seed << 6U) + (seed >> 2U);
        };
        mix(key.DirectionX);
        mix(key.DirectionY);
        mix(key.Constant);
        return seed;
    }
};

struct QuantizedEdge
{
    // Line 负责把共线边归组
    // 参数区间负责判断端点是否落入某条粗边内部
    QuantizedLineKey Line;
    long long MinParameter{0};
    long long MaxParameter{0};
};

float DistanceSquared(const glm::vec2& a, const glm::vec2& b)
{
    const glm::vec2 delta = b - a;
    return glm::dot(delta, delta);
}

bool SamePoint(const glm::vec2& a, const glm::vec2& b)
{
    // UV 细分会产生浮点中点，需要容差判断端点重合
    constexpr float Epsilon = 0.000001F;
    return DistanceSquared(a, b) <= Epsilon * Epsilon;
}

std::array<DomainEdge, 3> DomainEdges(const TriangleDomain& domain)
{
    return {
        DomainEdge{domain.A, domain.B},
        DomainEdge{domain.B, domain.C},
        DomainEdge{domain.C, domain.A},
    };
}

SplitEdge ChooseBaseEdge(const TriangleDomain& domain)
{
    // Classic ROAM 固定沿 base edge split，A/B 是 base 两端
    return SplitEdge{domain.A, domain.B, domain.C};
}

bool SameUndirectedEdge(const DomainEdge& left, const DomainEdge& right)
{
    // leaf neighbor 重建只匹配完整共享边，不把 T-junction 当作合法邻接
    // 两个方向都要匹配，因为相邻三角形通常以反向顺序保存共享边
    return (SamePoint(left.Start, right.Start) && SamePoint(left.End, right.End)) ||
           (SamePoint(left.Start, right.End) && SamePoint(left.End, right.Start));
}

long long AbsoluteGcd(long long a, long long b)
{
    return std::gcd(std::llabs(a), std::llabs(b));
}

QuantizedPoint QuantizePoint(const glm::vec2& point, int maxDepth)
{
    const auto scale = static_cast<long long>(1ULL << static_cast<unsigned int>(std::clamp(maxDepth, 0, 30)));
    return QuantizedPoint{
        static_cast<long long>(std::llround(static_cast<double>(point.x) * static_cast<double>(scale))),
        static_cast<long long>(std::llround(static_cast<double>(point.y) * static_cast<double>(scale))),
    };
}

QuantizedLineKey MakeLineKey(const QuantizedPoint& start, const QuantizedPoint& end)
{
    long long directionX = end.X - start.X;
    long long directionY = end.Y - start.Y;
    const long long divisor = std::max(AbsoluteGcd(directionX, directionY), 1LL);
    directionX /= divisor;
    directionY /= divisor;

    // 同一条无向直线必须得到唯一方向
    // 否则相邻三角形的反向边会落入不同桶
    if (directionX < 0 || (directionX == 0 && directionY < 0))
    {
        directionX = -directionX;
        directionY = -directionY;
    }

    return QuantizedLineKey{
        directionX,
        directionY,
        directionY * start.X - directionX * start.Y,
    };
}

long long ProjectToLineParameter(const QuantizedPoint& point, const QuantizedLineKey& line)
{
    return line.DirectionX * point.X + line.DirectionY * point.Y;
}

std::uint64_t LeftChildPathId(std::uint64_t parentPathId)
{
    // child path 保持二叉堆编码，便于跨帧 hysteresis 复用
    return parentPathId * 2ULL;
}

std::uint64_t RightChildPathId(std::uint64_t parentPathId)
{
    // right child 通过末位 1 与 left child 区分
    return parentPathId * 2ULL + 1ULL;
}

float ElapsedMilliseconds(std::chrono::steady_clock::time_point start, std::chrono::steady_clock::time_point end)
{
    const std::chrono::duration<float, std::milli> elapsed = end - start;
    return elapsed.count();
}
} // 匿名命名空间

Terrain::TerrainMeshData ClassicRoamMeshBuilder::Build(
    const Terrain::HeightMap& heightMap,
    float terrainSize,
    float heightScale,
    const glm::vec3& cameraPosition,
    const ClassicRoamSettings& settings)
{
    const auto updateStart = std::chrono::steady_clock::now();
    ++_buildSequence;
    const bool resetTopology = NeedsTopologyReset(heightMap, terrainSize, heightScale, settings);
    _heightMap = &heightMap;
    _settings = settings;
    // merge 阈值不能高于 split 阈值，否则同一帧可能反复 split / merge
    _settings.MergeThreshold = std::min(_settings.MergeThreshold, _settings.SplitThreshold);
    _stats = {};
    _currentSplitPaths.clear();
    _cameraPosition = cameraPosition;
    _terrainSize = terrainSize;
    _heightScale = heightScale;

    Terrain::TerrainMeshData meshData{};
    meshData.GridWidth = heightMap.Width();
    meshData.GridHeight = heightMap.Height();
    meshData.TerrainSize = terrainSize;
    meshData.HeightScale = heightScale;

    if (!heightMap.IsValid())
    {
        // 与规则网格 builder 保持空 mesh 失败语义
        return meshData;
    }

    if (resetTopology)
    {
        // 高度图或最大深度不兼容时才清空拓扑，普通相机移动保留树结构
        ResetTopology();
    }

    const auto mergeStart = std::chrono::steady_clock::now();
    // merge 先运行，避免刚 split 的 child 在同一帧被低阈值立即回收
    MergeWithDiamondQueue();
    const auto mergeEnd = std::chrono::steady_clock::now();

    const auto splitStart = std::chrono::steady_clock::now();
    // 阶段 2I 使用候选队列驱动 split，避免纯递归一次展开过多节点
    RefineWithSplitQueue(_rootA, _rootB);
    const auto splitEnd = std::chrono::steady_clock::now();

    if (_settings.EnableTopologyValidation)
    {
        // validator 是调试路径
        // 不参与默认修复逻辑
        // 结果通过 stats 暴露给 UI 和 smoke benchmark
        const auto validateStart = std::chrono::steady_clock::now();
        ValidateTopology();
        const auto validateEnd = std::chrono::steady_clock::now();
        _stats.ValidateMilliseconds = ElapsedMilliseconds(validateStart, validateEnd);
    }

    const auto emitStart = std::chrono::steady_clock::now();
    EmitLeafTriangles(meshData);
    const auto emitEnd = std::chrono::steady_clock::now();

    _stats.NodeCount = _nodes.size();
    _stats.ActiveTriangleCount = meshData.Indices.size() / 3U;
    std::vector<ClassicRoamNode*> activeLeaves;
    CollectLeafNodes(activeLeaves);
    _stats.MaxDepthReached = 0;
    for (const ClassicRoamNode* leaf : activeLeaves)
    {
        // 只统计 active leaf
        // node pool 中保留的 inactive child 不参与当前帧数据
        _stats.MaxDepthReached = std::max(_stats.MaxDepthReached, leaf->Depth);
        switch (ClassifyLeafDebug(*leaf))
        {
        case LeafDebugClass::Original:
            ++_stats.OriginalTriangleCount;
            break;
        case LeafDebugClass::Subdivided:
            ++_stats.SubdividedTriangleCount;
            break;
        case LeafDebugClass::Rebuilt:
            ++_stats.RebuiltTriangleCount;
            break;
        }
    }
    _stats.MergeMilliseconds = ElapsedMilliseconds(mergeStart, mergeEnd);
    _stats.SplitMilliseconds = ElapsedMilliseconds(splitStart, splitEnd);
    _stats.EmitMilliseconds = ElapsedMilliseconds(emitStart, emitEnd);
    _stats.UpdateMilliseconds = ElapsedMilliseconds(updateStart, std::chrono::steady_clock::now());
    CollectActiveSplitPaths();
    // hysteresis 只使用 merge 和 split 完成后的最终 active topology
    _previousSplitPaths = _currentSplitPaths;
    _topologyMaxDepth = _settings.MaxDepth;
    return meshData;
}

const ClassicRoamStats& ClassicRoamMeshBuilder::Stats() const
{
    return _stats;
}

ClassicRoamMeshBuilder::ClassicRoamNode* ClassicRoamMeshBuilder::AddNode(
    const TriangleDomain& domain,
    ClassicRoamNode* parent,
    int depth,
    std::uint64_t pathId)
{
    std::unique_ptr<ClassicRoamNode> node = std::make_unique<ClassicRoamNode>();
    node->Domain = domain;
    node->Parent = parent;
    node->Depth = depth;
    node->PathId = pathId;
    node->CreatedBuildId = _buildSequence;
    node->ActivatedBuildId = _buildSequence;
    node->GeometricError = ComputeGeometricError(domain);
    _stats.MaxDepthReached = std::max(_stats.MaxDepthReached, depth);

    // unique_ptr 池负责生命周期，节点之间保留 Classic ROAM 裸指针关系
    ClassicRoamNode* nodePointer = node.get();
    _nodes.push_back(std::move(node));
    return nodePointer;
}

void ClassicRoamMeshBuilder::ResetTopology()
{
    // ResetTopology 是唯一清空 node pool 的入口，避免普通 frame 破坏持久化拓扑
    _nodes.clear();
    _previousSplitPaths.clear();
    _currentSplitPaths.clear();

    // 两个根三角形共享对角线 base edge，构成 Classic ROAM 的根 diamond
    _rootA = AddNode(
        TriangleDomain{glm::vec2{0.0F, 1.0F}, glm::vec2{1.0F, 0.0F}, glm::vec2{0.0F, 0.0F}},
        nullptr,
        0,
        RootAPathId);
    _rootB = AddNode(
        TriangleDomain{glm::vec2{1.0F, 0.0F}, glm::vec2{0.0F, 1.0F}, glm::vec2{1.0F, 1.0F}},
        nullptr,
        0,
        RootBPathId);

    // 根节点跨共享 base edge 互为 base neighbor
    _rootA->BaseNeighbor = _rootB;
    _rootB->BaseNeighbor = _rootA;
    _topologyMaxDepth = _settings.MaxDepth;
}

bool ClassicRoamMeshBuilder::NeedsTopologyReset(
    const Terrain::HeightMap& heightMap,
    float terrainSize,
    float heightScale,
    const ClassicRoamSettings& settings) const
{
    if (_rootA == nullptr || _rootB == nullptr || _nodes.empty())
    {
        // 首帧没有 root diamond，必须初始化
        return true;
    }

    if (_heightMap != &heightMap)
    {
        // Height Map 变化会让几何误差缓存失效
        return true;
    }

    if (settings.MaxDepth < _topologyMaxDepth)
    {
        // 降低最大深度时，保守重建以清理过深的历史节点
        return true;
    }

    // terrain size 或 height scale 改变时，保守重建以避免旧 score 驱动错误 hysteresis
    return terrainSize != _terrainSize || heightScale != _heightScale;
}

void ClassicRoamMeshBuilder::RefineNode(ClassicRoamNode* node)
{
    if (node == nullptr)
    {
        return;
    }

    if (!IsLeaf(node))
    {
        RefineNode(node->LeftChild);
        RefineNode(node->RightChild);
        return;
    }

    if (!ShouldSplit(*node))
    {
        return;
    }

    if (!SplitNode(node, SplitReason::Requested, nullptr))
    {
        return;
    }

    RefineNode(node->LeftChild);
    RefineNode(node->RightChild);
}

void ClassicRoamMeshBuilder::RefineWithSplitQueue(ClassicRoamNode* rootA, ClassicRoamNode* rootB)
{
    // priority queue 把 split budget 用在最高 screen error 的 leaf 上
    // 避免递归遍历时某条局部路径抢占整帧预算
    struct SplitCandidate
    {
        float Score{0.0F};
        std::uint64_t Sequence{0};
        ClassicRoamNode* Node{nullptr};
    };

    struct CandidateCompare
    {
        bool operator()(const SplitCandidate& left, const SplitCandidate& right) const
        {
            if (left.Score == right.Score)
            {
                // sequence 让同分候选稳定排序
                // 减少相机静止时的拓扑抖动
                return left.Sequence > right.Sequence;
            }

            return left.Score < right.Score;
        }
    };

    std::priority_queue<SplitCandidate, std::vector<SplitCandidate>, CandidateCompare> candidates;
    std::uint64_t sequence = 0;

    const auto enqueueCandidate = [this, &candidates, &sequence](ClassicRoamNode* node) {
        // 只有 leaf 会进入候选队列，内部节点已经由 child 接管细分决策
        if (node == nullptr || !IsLeaf(node) || node->Depth >= _settings.MaxDepth)
        {
            return;
        }

        const float score = ComputeScreenErrorScore(*node);
        if (!ShouldSplitWithScore(*node, score))
        {
            return;
        }

        // 分数相同用 sequence 保持稳定顺序，避免相机不动时队列顺序跳动
        candidates.push(SplitCandidate{score, sequence++, node});
        _stats.CandidatePeakCount = std::max(_stats.CandidatePeakCount, candidates.size());
    };

    const auto enqueueActiveLeaves = [&enqueueCandidate, this](auto&& self, ClassicRoamNode* node) -> void {
        if (node == nullptr)
        {
            return;
        }

        if (IsLeaf(node))
        {
            // 持久拓扑中 root 通常已经 split，必须从当前 active leaf 启动队列
            enqueueCandidate(node);
            return;
        }

        // 递归深入 internal node，保证每个 active leaf 都能按新相机位置重新评估
        self(self, node->LeftChild);
        self(self, node->RightChild);
    };

    enqueueActiveLeaves(enqueueActiveLeaves, rootA);
    enqueueActiveLeaves(enqueueActiveLeaves, rootB);

    while (!candidates.empty())
    {
        const SplitCandidate candidate = candidates.top();
        candidates.pop();

        ClassicRoamNode* node = candidate.Node;
        // 候选可能已经被 forced split 拆掉，需要在弹出时再次确认
        if (node == nullptr || !IsLeaf(node))
        {
            // candidate 可能已经被 forced split 消耗
            continue;
        }

        const float score = ComputeScreenErrorScore(*node);
        // 分数会随 forced split 后的拓扑变化重新计算，避免使用过期候选
        if (!ShouldSplitWithScore(*node, score))
        {
            // score 在弹出时重算
            // 约束传播产生的新拓扑可能让旧候选过期
            continue;
        }

        if (_settings.SplitBudget > 0U && _stats.SplitCount >= _settings.SplitBudget)
        {
            // 预算耗尽时停止本次 build，下一次相机移动后继续按队列重建
            ++_stats.RejectedSplitCount;
            break;
        }

        ClassicRoamNode* baseNeighborBeforeSplit = node->BaseNeighbor;
        if (!SplitNode(node, SplitReason::Requested, nullptr))
        {
            ++_stats.RejectedSplitCount;
            continue;
        }

        enqueueCandidate(node->LeftChild);
        enqueueCandidate(node->RightChild);

        // forced split 可能先拆开 base neighbor，也需要把新 child 放回候选队列
        // 这样局部约束产生的新三角形不会停在过粗层级
        if (baseNeighborBeforeSplit != nullptr && !IsLeaf(baseNeighborBeforeSplit))
        {
            enqueueCandidate(baseNeighborBeforeSplit->LeftChild);
            enqueueCandidate(baseNeighborBeforeSplit->RightChild);
        }
    }
}

void ClassicRoamMeshBuilder::MergeWithDiamondQueue()
{
    // merge 从可回收 internal node 中收集候选
    // 低 error 先回收
    // 让远处细节比近处更早退回粗网格
    struct MergeCandidate
    {
        float Score{0.0F};
        ClassicRoamNode* Node{nullptr};
    };

    std::vector<MergeCandidate> candidates;
    const auto collectCandidates = [this, &candidates](auto&& self, ClassicRoamNode* node) -> void {
        // merge candidate 必须从 active internal node 中收集
        if (node == nullptr || IsLeaf(node))
        {
            return;
        }

        if (CanMergeNode(node))
        {
            candidates.push_back(MergeCandidate{ComputeScreenErrorScore(*node), node});
        }

        self(self, node->LeftChild);
        self(self, node->RightChild);
    };

    collectCandidates(collectCandidates, _rootA);
    collectCandidates(collectCandidates, _rootB);

    std::sort(
        candidates.begin(),
        candidates.end(),
        [](const MergeCandidate& left, const MergeCandidate& right) {
            // 误差越低越优先 merge，减少远处无意义细节
            return left.Score < right.Score;
        });

    for (const MergeCandidate& candidate : candidates)
    {
        // 候选列表排序后，前面的 merge 可能改变后面节点的 active 状态
        ClassicRoamNode* node = candidate.Node;
        if (!CanMergeNode(node))
        {
            continue;
        }

        if (!MergeNodeOrDiamond(node))
        {
            ++_stats.RejectedMergeCount;
        }
    }
}

bool ClassicRoamMeshBuilder::SplitNode(
    ClassicRoamNode* node,
    SplitReason reason,
    ClassicRoamNode* forcedFrom)
{
    if (!IsLeaf(node))
    {
        return false;
    }

    if (node->Depth >= _settings.MaxDepth)
    {
        ++_stats.RejectedSplitCount;
        return false;
    }

    ClassicRoamNode* baseNeighbor = node->BaseNeighbor;
    if (_settings.EnableLocalConstraints)
    {
        int guard = 0;
        // baseNeighbor 不是互指关系时，先沿邻接链追到合法 diamond
        // guard 防止损坏拓扑导致无限递归
        while (baseNeighbor != nullptr &&
               baseNeighbor != forcedFrom &&
               baseNeighbor->BaseNeighbor != node &&
               guard < _settings.MaxDepth + 2)
        {
            // 经典 ROAM 要求 baseNeighbor 先回到互为 base 的 diamond 关系
            ++_stats.ConstraintPassCount;
            if (!SplitNode(baseNeighbor, SplitReason::ForcedByBaseNeighbor, node))
            {
                return false;
            }

            // split 可能通过 ReplaceNeighborReference 改写 node 的 baseNeighbor
            baseNeighbor = node->BaseNeighbor;
            ++guard;
        }
    }

    if (_settings.EnableLocalConstraints && baseNeighbor != nullptr && IsLeaf(baseNeighbor) && baseNeighbor != forcedFrom)
    {
        // Classic ROAM 先补齐 base neighbor，保证旧 base edge 两侧一起 split 成 diamond
        // forcedFrom 防止互为 base neighbor 的两个 leaf 递归回跳
        ++_stats.ConstraintPassCount;
        if (!SplitNode(baseNeighbor, SplitReason::ForcedByBaseNeighbor, node))
        {
            return false;
        }
        // forced split 完成后刷新指针，后续 LinkSplitNeighbors 使用最新 diamond
        baseNeighbor = node->BaseNeighbor;
    }

    const std::uint64_t parentPathId = node->PathId;
    if (node->LeftChild == nullptr || node->RightChild == nullptr)
    {
        // 首次 split 创建 child，后续 merge 后再次 split 会复用旧 child
        const TriangleDomain domain = node->Domain;
        const int childDepth = node->Depth + 1;
        const SplitEdge edge = ChooseBaseEdge(domain);
        const glm::vec2 midpoint = (edge.Start + edge.End) * 0.5F;

        // 子节点继续把 A/B 作为 base edge，保留经典 bintree 递归语义
        const TriangleDomain leftDomain{edge.Apex, edge.Start, midpoint};
        const TriangleDomain rightDomain{edge.End, edge.Apex, midpoint};
        node->LeftChild = AddNode(leftDomain, node, childDepth, LeftChildPathId(parentPathId));
        node->RightChild = AddNode(rightDomain, node, childDepth, RightChildPathId(parentPathId));
    }

    node->IsSplit = true;
    node->SplitBuildId = _buildSequence;
    // 重新激活 child 前清空旧邻接，避免历史 merge 状态污染本次 split
    // child 指针复用是性能优化
    // 但 neighbor 指针必须按当前 active topology 重建
    node->LeftChild->BaseNeighbor = nullptr;
    node->LeftChild->LeftNeighbor = nullptr;
    node->LeftChild->RightNeighbor = nullptr;
    node->RightChild->BaseNeighbor = nullptr;
    node->RightChild->LeftNeighbor = nullptr;
    node->RightChild->RightNeighbor = nullptr;
    node->LeftChild->ActivatedBuildId = _buildSequence;
    node->RightChild->ActivatedBuildId = _buildSequence;
    node->LeftChild->ActivatedByForcedSplit = reason != SplitReason::Requested;
    node->RightChild->ActivatedByForcedSplit = reason != SplitReason::Requested;

    LinkSplitNeighbors(node, baseNeighbor);
    _currentSplitPaths.insert(parentPathId);
    ++_stats.SplitCount;
    if (reason != SplitReason::Requested)
    {
        ++_stats.ForcedSplitCount;
    }
    return true;
}

void ClassicRoamMeshBuilder::LinkSplitNeighbors(ClassicRoamNode* node, ClassicRoamNode* baseNeighbor)
{
    ClassicRoamNode* leftChild = node->LeftChild;
    ClassicRoamNode* rightChild = node->RightChild;
    if (leftChild == nullptr || rightChild == nullptr)
    {
        return;
    }

    // left child 的 left edge 与 right child 的 right edge 共享 split 中线
    leftChild->LeftNeighbor = rightChild;
    rightChild->RightNeighbor = leftChild;

    // child 的 base edge 分别来自父节点的 left edge 和 right edge
    leftChild->BaseNeighbor = node->LeftNeighbor;
    rightChild->BaseNeighbor = node->RightNeighbor;
    ReplaceNeighborReference(node->LeftNeighbor, node, leftChild);
    ReplaceNeighborReference(node->RightNeighbor, node, rightChild);

    if (baseNeighbor == nullptr || IsLeaf(baseNeighbor))
    {
        // 对侧仍是粗 leaf 时没有可连接的 child pair
        // 后续 forced split 会补齐 diamond
        return;
    }

    // baseNeighbor 已经 split 时，四个 child 共同组成无裂缝 diamond
    leftChild->RightNeighbor = baseNeighbor->RightChild;
    rightChild->LeftNeighbor = baseNeighbor->LeftChild;
    if (baseNeighbor->RightChild != nullptr)
    {
        baseNeighbor->RightChild->LeftNeighbor = leftChild;
    }

    if (baseNeighbor->LeftChild != nullptr)
    {
        baseNeighbor->LeftChild->RightNeighbor = rightChild;
    }
}

void ClassicRoamMeshBuilder::ReplaceNeighborReference(
    ClassicRoamNode* neighbor,
    ClassicRoamNode* oldNode,
    ClassicRoamNode* newNode) const
{
    if (neighbor == nullptr)
    {
        return;
    }

    // 相邻 leaf 仍指向旧节点时，把它改到 split 后共享完整边的 child
    if (neighbor->BaseNeighbor == oldNode)
    {
        neighbor->BaseNeighbor = newNode;
    }

    if (neighbor->LeftNeighbor == oldNode)
    {
        neighbor->LeftNeighbor = newNode;
    }

    if (neighbor->RightNeighbor == oldNode)
    {
        neighbor->RightNeighbor = newNode;
    }
}

bool ClassicRoamMeshBuilder::CanMergeNode(const ClassicRoamNode* node) const
{
    // 只能回收已经 split 的 parent
    if (node == nullptr || IsLeaf(node))
    {
        return false;
    }

    if (node->LeftChild == nullptr || node->RightChild == nullptr)
    {
        return false;
    }

    if (!IsLeaf(node->LeftChild) || !IsLeaf(node->RightChild))
    {
        // child 还有更深细分时，必须先从更深处开始 merge
        return false;
    }

    if (ComputeScreenErrorScore(*node) > _settings.MergeThreshold)
    {
        // parent 自身误差还高时，回收会造成明显 LOD 退化
        return false;
    }

    const ClassicRoamNode* baseNeighbor = node->BaseNeighbor;
    if (baseNeighbor == nullptr || IsLeaf(baseNeighbor))
    {
        // 边界边或对侧已是粗 leaf 时，可以只回收当前 sibling pair
        return true;
    }

    // 非互指 diamond 不能 merge，否则会制造一大边贴多小边
    if (baseNeighbor->BaseNeighbor != node)
    {
        // 非互为 base 的邻接不是合法 diamond，单侧 merge 会制造 T-junction
        return false;
    }

    if (baseNeighbor->LeftChild == nullptr || baseNeighbor->RightChild == nullptr)
    {
        return false;
    }

    if (!IsLeaf(baseNeighbor->LeftChild) || !IsLeaf(baseNeighbor->RightChild))
    {
        // 对侧 diamond 还有更细 leaf 时，不能先回收当前侧
        return false;
    }

    return ComputeScreenErrorScore(*baseNeighbor) <= _settings.MergeThreshold;
}

void ClassicRoamMeshBuilder::MergeSingleNode(ClassicRoamNode* node)
{
    if (node == nullptr || node->LeftChild == nullptr || node->RightChild == nullptr)
    {
        return;
    }

    ClassicRoamNode* leftChild = node->LeftChild;
    ClassicRoamNode* rightChild = node->RightChild;
    ClassicRoamNode* newLeftNeighbor = leftChild->BaseNeighbor;
    ClassicRoamNode* newRightNeighbor = rightChild->BaseNeighbor;

    // parent 的 left/right 边分别来自两个 child 的 base 边
    // 外部 neighbor 必须改指向 parent，不能继续指向 inactive child
    // 否则 validator 会发现 neighbor 指向非 active leaf
    ReplaceNeighborReference(newLeftNeighbor, leftChild, node);
    ReplaceNeighborReference(newRightNeighbor, rightChild, node);
    node->LeftNeighbor = newLeftNeighbor;
    node->RightNeighbor = newRightNeighbor;
    // child 指针保留但不再 active，后续重新 split 可复用 child 对象
    node->IsSplit = false;
    node->ActivatedBuildId = _buildSequence;
    node->MergeBuildId = _buildSequence;
    node->ActivatedByForcedSplit = false;
    ++_stats.MergeCount;
}

bool ClassicRoamMeshBuilder::MergeNodeOrDiamond(ClassicRoamNode* node)
{
    if (!CanMergeNode(node))
    {
        return false;
    }

    ClassicRoamNode* baseNeighbor = node->BaseNeighbor;
    if (baseNeighbor != nullptr && !IsLeaf(baseNeighbor))
    {
        // 完整 diamond merge 要同时回收当前 parent 和 base parent
        // 单侧回收会让对侧 child 贴到粗边上
        if (!CanMergeNode(baseNeighbor) || baseNeighbor->BaseNeighbor != node)
        {
            return false;
        }

        // 先固定 parent 之间的 base 互指，再回收两侧 child
        // MergeSingleNode 不会改 baseNeighbor，因此互指关系会保留下来
        node->BaseNeighbor = baseNeighbor;
        baseNeighbor->BaseNeighbor = node;
        MergeSingleNode(node);
        MergeSingleNode(baseNeighbor);
        node->BaseNeighbor = baseNeighbor;
        baseNeighbor->BaseNeighbor = node;
        return true;
    }

    MergeSingleNode(node);
    return true;
}

void ClassicRoamMeshBuilder::CollectLeafNodes(std::vector<ClassicRoamNode*>& leafNodes) const
{
    // leaf 集合是当前 active mesh 的拓扑基础
    leafNodes.clear();
    leafNodes.reserve(_nodes.size());
    // 只能从 root 递归收集，不能遍历整个 node pool
    CollectLeafNodesFrom(_rootA, leafNodes);
    CollectLeafNodesFrom(_rootB, leafNodes);
}

void ClassicRoamMeshBuilder::CollectLeafNodesFrom(ClassicRoamNode* node, std::vector<ClassicRoamNode*>& leafNodes) const
{
    if (node == nullptr)
    {
        return;
    }

    if (IsLeaf(node))
    {
        // inactive child 可能还留在 node pool 中，但不会从 root active 路径抵达
        leafNodes.push_back(node);
        return;
    }

    CollectLeafNodesFrom(node->LeftChild, leafNodes);
    CollectLeafNodesFrom(node->RightChild, leafNodes);
}

void ClassicRoamMeshBuilder::CollectActiveSplitPaths()
{
    // active split path 用于 hysteresis，必须反映 merge 后的当前拓扑
    _currentSplitPaths.clear();
    _stats.ActiveSplitCount = 0;
    CollectActiveSplitPathsFrom(_rootA);
    CollectActiveSplitPathsFrom(_rootB);
}

void ClassicRoamMeshBuilder::CollectActiveSplitPathsFrom(const ClassicRoamNode* node)
{
    if (node == nullptr || IsLeaf(node))
    {
        // leaf 没有 child，不属于 split path
        return;
    }

    _currentSplitPaths.insert(node->PathId);
    ++_stats.ActiveSplitCount;
    CollectActiveSplitPathsFrom(node->LeftChild);
    CollectActiveSplitPathsFrom(node->RightChild);
}

void ClassicRoamMeshBuilder::ValidateTopology()
{
    // validator 使用量化边线索引检查裂缝，避免 leaf 之间两两扫描
    // 这里故意独立于 neighbor 指针做几何裂缝检测
    // 可以同时发现 neighbor 链路正确但 leaf 尺度不一致的问题
    std::vector<ClassicRoamNode*> leafNodes;
    CollectLeafNodes(leafNodes);
    std::unordered_set<const ClassicRoamNode*> leafSet;
    leafSet.reserve(leafNodes.size());

    for (ClassicRoamNode* node : leafNodes)
    {
        // leafSet 用于判断 neighbor 是否仍然指向 active leaf
        leafSet.insert(node);
    }

    const auto validateNeighbor = [&leafSet](const ClassicRoamNode* owner, const ClassicRoamNode* neighbor, const DomainEdge& edge) {
        // 边界边允许为空，非空 neighbor 必须是 active leaf
        if (neighbor == nullptr)
        {
            return false;
        }

        if (leafSet.find(neighbor) == leafSet.end())
        {
            return false;
        }

        for (const DomainEdge& neighborEdge : DomainEdges(neighbor->Domain))
        {
            if (SameUndirectedEdge(edge, neighborEdge))
            {
                // 共享边成立后还要检查对侧是否能反向找到 owner
                return neighbor->BaseNeighbor == owner ||
                       neighbor->LeftNeighbor == owner ||
                       neighbor->RightNeighbor == owner;
            }
        }

        return false;
    };

    std::unordered_map<QuantizedLineKey, std::vector<long long>, QuantizedLineKeyHash> lineVertices;
    lineVertices.reserve(leafNodes.size() * 3U);
    std::vector<QuantizedEdge> quantizedEdges;
    quantizedEdges.reserve(leafNodes.size() * 3U);

    for (ClassicRoamNode* node : leafNodes)
    {
        const std::array<DomainEdge, 3> edges = DomainEdges(node->Domain);

        for (const DomainEdge& edge : edges)
        {
            // 每条边都映射到量化直线
            // 同线端点集合用于检测粗边内部是否存在细边端点
            const QuantizedPoint start = QuantizePoint(edge.Start, _settings.MaxDepth);
            const QuantizedPoint end = QuantizePoint(edge.End, _settings.MaxDepth);
            const QuantizedLineKey line = MakeLineKey(start, end);
            const long long startParameter = ProjectToLineParameter(start, line);
            const long long endParameter = ProjectToLineParameter(end, line);
            QuantizedEdge quantizedEdge{};
            quantizedEdge.Line = line;
            quantizedEdge.MinParameter = std::min(startParameter, endParameter);
            quantizedEdge.MaxParameter = std::max(startParameter, endParameter);
            quantizedEdges.push_back(quantizedEdge);

            // 同一直线上的端点参数可用于快速发现粗边内部是否被其他 leaf 顶点切开
            std::vector<long long>& vertexParameters = lineVertices[line];
            vertexParameters.push_back(startParameter);
            vertexParameters.push_back(endParameter);
        }
    }

    for (auto& [line, vertexParameters] : lineVertices)
    {
        (void)line;
        std::sort(vertexParameters.begin(), vertexParameters.end());
        vertexParameters.erase(std::unique(vertexParameters.begin(), vertexParameters.end()), vertexParameters.end());
    }

    for (const QuantizedEdge& edge : quantizedEdges)
    {
        const auto lineIt = lineVertices.find(edge.Line);
        if (lineIt == lineVertices.end())
        {
            continue;
        }

        const std::vector<long long>& vertexParameters = lineIt->second;
        const auto interiorIt = std::upper_bound(vertexParameters.begin(), vertexParameters.end(), edge.MinParameter);
        if (interiorIt != vertexParameters.end() && *interiorIt < edge.MaxParameter)
        {
            // validator 只记录 T-junction，不主动 split 修复
            // 修复仍由 split 约束传播负责
            ++_stats.TjunctionCount;
            ++_stats.CrackRiskCount;
        }
    }

    for (ClassicRoamNode* node : leafNodes)
    {
        const std::array<DomainEdge, 3> edges = DomainEdges(node->Domain);

        // 只验证非空 neighbor，边界边允许为空
        if (node->BaseNeighbor != nullptr && !validateNeighbor(node, node->BaseNeighbor, edges[0]))
        {
            ++_stats.InvalidNeighborCount;
        }

        if (node->RightNeighbor != nullptr && !validateNeighbor(node, node->RightNeighbor, edges[1]))
        {
            ++_stats.InvalidNeighborCount;
        }

        if (node->LeftNeighbor != nullptr && !validateNeighbor(node, node->LeftNeighbor, edges[2]))
        {
            ++_stats.InvalidNeighborCount;
        }
    }

    if (_rootA == nullptr || _rootB == nullptr || _rootA->BaseNeighbor != _rootB || _rootB->BaseNeighbor != _rootA)
    {
        // 根 diamond 互指是所有后续 diamond 约束的基础
        ++_stats.InvalidTopologyCount;
    }

    for (const std::unique_ptr<ClassicRoamNode>& ownedNode : _nodes)
    {
        const ClassicRoamNode* node = ownedNode.get();
        if (node == nullptr)
        {
            ++_stats.InvalidTopologyCount;
            continue;
        }

        if (node->IsSplit && (node->LeftChild == nullptr || node->RightChild == nullptr))
        {
            ++_stats.InvalidTopologyCount;
        }

        if (node->LeftChild != nullptr && node->LeftChild->Parent != node)
        {
            ++_stats.InvalidTopologyCount;
        }

        if (node->RightChild != nullptr && node->RightChild->Parent != node)
        {
            ++_stats.InvalidTopologyCount;
        }

        if (node != _rootA && node != _rootB && node->Parent == nullptr)
        {
            ++_stats.InvalidTopologyCount;
        }
    }
}

void ClassicRoamMeshBuilder::EmitLeafTriangles(Terrain::TerrainMeshData& meshData) const
{
    std::vector<ClassicRoamNode*> leafNodes;
    CollectLeafNodes(leafNodes);

    // 当前 mesh 直接复制 active leaf 顶点，后续可加顶点缓存减少重复顶点
    for (const ClassicRoamNode* node : leafNodes)
    {
        EmitNode(*node, meshData);
    }
}

void ClassicRoamMeshBuilder::EmitNode(const ClassicRoamNode& node, Terrain::TerrainMeshData& meshData) const
{
    if (IsLeaf(&node))
    {
        EmitDomainTriangle(node, meshData);
    }
}

void ClassicRoamMeshBuilder::EmitDomainTriangle(const ClassicRoamNode& node, Terrain::TerrainMeshData& meshData) const
{
    const auto baseIndex = static_cast<std::uint32_t>(meshData.Vertices.size());
    const TriangleDomain& domain = node.Domain;
    const std::array<glm::vec2, 3> uvs{domain.A, domain.B, domain.C};
    const glm::vec3 debugColor = DebugColorForLeaf(node);
    const float debugHighlight = DebugHighlightForLeaf(node);

    for (const glm::vec2& uv : uvs)
    {
        // ROAM leaf 顶点从 Height Map 即时采样，保证 split 后新点高度正确
        // 不缓存顶点能避免 merge/split 后共享顶点生命周期复杂化
        Terrain::TerrainMeshVertex vertex{};
        vertex.Position = DomainToWorld(uv);
        vertex.Normal = SampleNormal(uv);
        vertex.TexCoord = uv;
        vertex.Height = _heightMap->SampleBilinear(uv.x, uv.y);
        vertex.DebugColor = debugColor;
        vertex.DebugHighlight = debugHighlight;
        meshData.Vertices.push_back(vertex);
    }

    const glm::vec3 edge0 = meshData.Vertices[baseIndex + 1U].Position - meshData.Vertices[baseIndex].Position;
    const glm::vec3 edge1 = meshData.Vertices[baseIndex + 2U].Position - meshData.Vertices[baseIndex].Position;
    const bool pointsTowardPositiveY = glm::cross(edge0, edge1).y >= 0.0F;

    // 输出绕序统一朝向正 Y，后续开启 face culling 时不会和规则网格相反
    meshData.Indices.push_back(baseIndex);
    if (pointsTowardPositiveY)
    {
        // 当前 domain 顺序已经和规则网格方向一致
        meshData.Indices.push_back(baseIndex + 1U);
        meshData.Indices.push_back(baseIndex + 2U);
    }
    else
    {
        // 交换后两个点即可翻转三角面方向
        meshData.Indices.push_back(baseIndex + 2U);
        meshData.Indices.push_back(baseIndex + 1U);
    }
}

bool ClassicRoamMeshBuilder::ShouldSplit(const ClassicRoamNode& node) const
{
    // 最大深度限制优先于误差判断，避免相机贴近时无限细分
    if (node.Depth >= _settings.MaxDepth)
    {
        return false;
    }

    return ShouldSplitWithScore(node, ComputeScreenErrorScore(node));
}

bool ClassicRoamMeshBuilder::ShouldSplitWithScore(const ClassicRoamNode& node, float screenErrorScore) const
{
    if (node.Depth >= _settings.MaxDepth)
    {
        return false;
    }

    if (screenErrorScore > _settings.SplitThreshold)
    {
        return true;
    }

    if (screenErrorScore < _settings.MergeThreshold)
    {
        return false;
    }

    // hysteresis 区间沿用上一帧 split 状态，降低 split/merge 抖动
    // 这也是 fixed camera benchmark 稳定的重要条件
    return WasSplitLastFrame(node);
}

bool ClassicRoamMeshBuilder::WasSplitLastFrame(const ClassicRoamNode& node) const
{
    return _previousSplitPaths.find(node.PathId) != _previousSplitPaths.end();
}

ClassicRoamMeshBuilder::LeafDebugClass ClassicRoamMeshBuilder::ClassifyLeafDebug(const ClassicRoamNode& node) const
{
    if (node.ActivatedBuildId == _buildSequence || node.MergeBuildId == _buildSequence)
    {
        return LeafDebugClass::Rebuilt;
    }

    if (node.Depth > 0)
    {
        return LeafDebugClass::Subdivided;
    }

    return LeafDebugClass::Original;
}

glm::vec3 ClassicRoamMeshBuilder::DebugColorForLeaf(const ClassicRoamNode& node) const
{
    const float depthRatio = std::clamp(
        static_cast<float>(node.Depth) / static_cast<float>(std::max(_settings.MaxDepth, 1)),
        0.0F,
        1.0F);

    switch (ClassifyLeafDebug(node))
    {
    case LeafDebugClass::Original:
        return glm::vec3{0.28F, 0.34F, 0.30F};
    case LeafDebugClass::Subdivided:
        return glm::mix(glm::vec3{0.08F, 0.72F, 0.62F}, glm::vec3{0.10F, 0.34F, 0.95F}, depthRatio);
    case LeafDebugClass::Rebuilt:
        if (node.ActivatedByForcedSplit)
        {
            return glm::mix(glm::vec3{0.96F, 0.34F, 0.90F}, glm::vec3{0.96F, 0.16F, 0.42F}, depthRatio);
        }

        return glm::mix(glm::vec3{1.0F, 0.68F, 0.15F}, glm::vec3{1.0F, 0.34F, 0.10F}, depthRatio);
    }

    return glm::vec3{0.28F, 0.34F, 0.30F};
}

float ClassicRoamMeshBuilder::DebugHighlightForLeaf(const ClassicRoamNode& node) const
{
    switch (ClassifyLeafDebug(node))
    {
    case LeafDebugClass::Original:
        return 0.35F;
    case LeafDebugClass::Subdivided:
        return 0.70F;
    case LeafDebugClass::Rebuilt:
        return 1.0F;
    }

    return 0.35F;
}

float ClassicRoamMeshBuilder::ComputeGeometricError(const TriangleDomain& domain) const
{
    const float heightA = _heightMap->SampleBilinear(domain.A.x, domain.A.y);
    const float heightB = _heightMap->SampleBilinear(domain.B.x, domain.B.y);
    const float heightC = _heightMap->SampleBilinear(domain.C.x, domain.C.y);

    // 边中点误差能捕获边界起伏，避免只看 base edge
    const auto edgeMidpointError = [this](const glm::vec2& start, const glm::vec2& end, float startHeight, float endHeight) {
        const glm::vec2 midpoint = (start + end) * 0.5F;
        const float midpointHeight = _heightMap->SampleBilinear(midpoint.x, midpoint.y);
        const float interpolatedHeight = (startHeight + endHeight) * 0.5F;
        return std::abs(midpointHeight - interpolatedHeight);
    };

    const glm::vec2 centroid = (domain.A + domain.B + domain.C) / 3.0F;
    const float centroidHeight = _heightMap->SampleBilinear(centroid.x, centroid.y);
    const float centroidInterpolatedHeight = (heightA + heightB + heightC) / 3.0F;

    // 采样三条边中点和重心，避免非 base 边或三角形内部起伏被漏掉
    return std::max({
        edgeMidpointError(domain.A, domain.B, heightA, heightB),
        edgeMidpointError(domain.B, domain.C, heightB, heightC),
        edgeMidpointError(domain.C, domain.A, heightC, heightA),
        std::abs(centroidHeight - centroidInterpolatedHeight),
    });
}

float ClassicRoamMeshBuilder::ComputeScreenErrorScore(const ClassicRoamNode& node) const
{
    const glm::vec3 a = DomainToWorld(node.Domain.A);
    const glm::vec3 b = DomainToWorld(node.Domain.B);
    const glm::vec3 c = DomainToWorld(node.Domain.C);
    // 使用三角形中心估算视距，足够支撑阶段 2 的 LOD 展示
    const glm::vec3 center = (a + b + c) / 3.0F;
    const float distance = std::max(glm::length(center - _cameraPosition), 0.05F);
    const float worldError = node.GeometricError * _heightScale;
    const float longestEdgeLength = std::max({
        glm::length(a - b),
        glm::length(b - c),
        glm::length(c - a),
    });
    constexpr float ProjectedEdgeWeight = 0.20F;
    const float heightErrorScore = worldError * _settings.DistanceScale / distance;
    const float edgeLengthScore = longestEdgeLength * ProjectedEdgeWeight / distance;

    // 高度误差负责地形起伏，边长项保证近处平缓地形也会继续细分
    // 两者取最大值，避免平地近处被过早 merge
    return std::max(heightErrorScore, edgeLengthScore);
}

glm::vec3 ClassicRoamMeshBuilder::DomainToWorld(const glm::vec2& uv) const
{
    // 世界空间仍以地形中心为原点，方便复用阶段 1 相机和光照
    const float height = _heightMap->SampleBilinear(uv.x, uv.y);
    return glm::vec3{
        (uv.x - 0.5F) * _terrainSize,
        height * _heightScale,
        (uv.y - 0.5F) * _terrainSize,
    };
}

glm::vec3 ClassicRoamMeshBuilder::SampleNormal(const glm::vec2& uv) const
{
    // 法线从 Height Map 梯度估计，不依赖相邻 leaf 拓扑
    const float stepU = 1.0F / static_cast<float>(std::max(_heightMap->Width() - 1, 1));
    const float stepV = 1.0F / static_cast<float>(std::max(_heightMap->Height() - 1, 1));
    const float left = _heightMap->SampleBilinear(uv.x - stepU, uv.y);
    const float right = _heightMap->SampleBilinear(uv.x + stepU, uv.y);
    const float down = _heightMap->SampleBilinear(uv.x, uv.y - stepV);
    const float up = _heightMap->SampleBilinear(uv.x, uv.y + stepV);

    const glm::vec3 tangentX{stepU * 2.0F * _terrainSize, (right - left) * _heightScale, 0.0F};
    const glm::vec3 tangentZ{0.0F, (up - down) * _heightScale, stepV * 2.0F * _terrainSize};
    const glm::vec3 normal = glm::cross(tangentZ, tangentX);

    // 极端退化时回退到竖直法线，避免 shader 中出现 NaN
    if (glm::dot(normal, normal) <= std::numeric_limits<float>::epsilon())
    {
        return glm::vec3{0.0F, 1.0F, 0.0F};
    }

    return glm::normalize(normal);
}

bool ClassicRoamMeshBuilder::IsLeaf(const ClassicRoamNode* node) const
{
    if (node == nullptr)
    {
        return false;
    }

    return !node->IsSplit;
}
} // 命名空间 ParallelRoam::Algorithms::ClassicRoam
