#include "algorithms/data_oriented_roam/DataOrientedRoamMeshBuilder.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <functional>
#include <limits>
#include <numeric>
#include <queue>
#include <unordered_map>

namespace ParallelRoam::Algorithms::DataOrientedRoam
{
namespace
{
// 3A 先把 Classic 指针树换成稳定 index pool
// 并行和 SoA 拆分留到后续阶段
constexpr std::uint64_t RootAPathId = 1ULL;

// 两棵根树的 path id 必须隔开
// 否则跨帧 split 记忆会碰撞
constexpr std::uint64_t RootBPathId = 1ULL << 32U;

// 精确容量估算只在安全移位范围内使用
constexpr int ExactReserveMaxDepth = 20;

// 大深度使用保守 fallback 避免容量计算溢出
constexpr std::size_t LargeDepthReserveFallback = 1'000'000U;

// split 只沿当前三角形 base edge 执行
// Start 和 End 是 base 两端
// Apex 是对侧顶点
struct SplitEdge
{
    glm::vec2 Start{0.0F};
    glm::vec2 End{0.0F};
    glm::vec2 Apex{0.0F};
};

// validator 使用完整 leaf 边界检测共享边和 T-junction
// 这里保留连续 UV 坐标
// 量化只发生在裂缝扫描阶段
struct DomainEdge
{
    glm::vec2 Start{0.0F};
    glm::vec2 End{0.0F};
};

// UV 坐标会在多次二分后产生浮点中点
// validator 先量化到 maxDepth 网格再比较
// 这样能避免浮点微小误差让同一条边匹配失败
struct QuantizedPoint
{
    long long X{0};
    long long Y{0};
};

// 同一直线上的边共用 LineKey
// MinParameter 和 MaxParameter 再描述这条线段的区间
// 这样可以用一维参数查找粗边内部是否被更细顶点切开
struct QuantizedLineKey
{
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

// 自定义 hash 让 validator 可以把量化直线放进 unordered_map
// 这里不追求加密质量
// 只需要在大量 leaf 边上保持分布稳定
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

// QuantizedEdge 保存某条 leaf 边在量化直线上的闭区间
// 如果同一直线上存在落在区间内部的端点
// 说明当前拓扑存在潜在 T-junction
struct QuantizedEdge
{
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
    // UV 中点由浮点计算产生
    // 使用容差能避免同一几何点因为舍入误差被判为不同点
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
    // Classic ROAM 固定把 A-B 作为 base edge
    // DOD 版必须保留这个约定才能和 Classic benchmark 对齐
    return SplitEdge{domain.A, domain.B, domain.C};
}

bool SameUndirectedEdge(const DomainEdge& left, const DomainEdge& right)
{
    // 邻接三角形通常以反向绕序保存共享边
    // validator 因此必须同时接受两个方向
    return (SamePoint(left.Start, right.Start) && SamePoint(left.End, right.End)) ||
           (SamePoint(left.Start, right.End) && SamePoint(left.End, right.Start));
}

long long AbsoluteGcd(long long a, long long b)
{
    return std::gcd(std::llabs(a), std::llabs(b));
}

QuantizedPoint QuantizePoint(const glm::vec2& point, int maxDepth)
{
    // maxDepth 决定理论最细网格
    // clamp 到 30 避免左移溢出 long long 的安全区间
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

    // 方向归一到统一半平面
    // 同一条无向直线才能得到相同 key
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
    // path id 使用二叉堆编码
    // merge 后重新 split 可以复用上一帧 hysteresis 状态
    return parentPathId * 2ULL;
}

std::uint64_t RightChildPathId(std::uint64_t parentPathId)
{
    // right child 通过末位 1 和 left child 区分
    return parentPathId * 2ULL + 1ULL;
}

float ElapsedMilliseconds(std::chrono::steady_clock::time_point start, std::chrono::steady_clock::time_point end)
{
    const std::chrono::duration<float, std::milli> elapsed = end - start;
    return elapsed.count();
}

std::size_t ExactBintreeNodeCapacity(int maxDepth)
{
    // 两棵根树都可能展开成完整二叉树
    // 3A 预分配容量按完整 bintree 上界估算
    const int safeDepth = std::clamp(maxDepth, 0, ExactReserveMaxDepth);
    const std::size_t nodesPerRoot = (std::size_t{1} << static_cast<unsigned int>(safeDepth + 1)) - 1U;
    return nodesPerRoot * 2U;
}
} // 匿名命名空间

Terrain::TerrainMeshData DataOrientedRoamMeshBuilder::Build(
    const Terrain::HeightMap& heightMap,
    float terrainSize,
    float heightScale,
    const glm::vec3& cameraPosition,
    const DataOrientedRoamSettings& settings)
{
    const auto updateStart = std::chrono::steady_clock::now();
    ++_buildSequence;
    const bool resetTopology = NeedsTopologyReset(heightMap, terrainSize, heightScale, settings);
    _heightMap = &heightMap;
    _settings = settings;
    // merge 阈值不能高于 split 阈值
    // 否则同一帧可能在 hysteresis 区间反复展开和回收
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
        // 保持返回空 mesh 的语义和 Classic builder 一致
        return meshData;
    }

    ReserveNodePool();

    if (resetTopology)
    {
        // HeightMap 或几何尺度变化会让缓存误差失效
        ResetTopology();
        // 降低 maxDepth 时也需要丢弃过深历史节点
    }

    // merge 先于 split
    // 这样远处旧细节会先回收
    // 新 split 不会在同一帧立刻被低阈值撤销
    const auto mergeStart = std::chrono::steady_clock::now();
    MergeWithDiamondQueue();
    const auto mergeEnd = std::chrono::steady_clock::now();

    const auto splitStart = std::chrono::steady_clock::now();
    RefineWithSplitQueue(_rootA, _rootB);
    const auto splitEnd = std::chrono::steady_clock::now();

    if (_settings.EnableTopologyValidation)
    {
        // validator 是全局扫描
        const auto validateStart = std::chrono::steady_clock::now();
        ValidateTopology();
        // 默认只在 smoke benchmark 和 debug 场景开启
        const auto validateEnd = std::chrono::steady_clock::now();
        _stats.ValidateMilliseconds = ElapsedMilliseconds(validateStart, validateEnd);
    }

    const auto emitStart = std::chrono::steady_clock::now();
    EmitLeafTriangles(meshData);
    const auto emitEnd = std::chrono::steady_clock::now();

    _stats.NodeCount = _nodes.size();
    _stats.ReservedNodeCapacity = _nodes.capacity();
    _stats.ActiveTriangleCount = meshData.Indices.size() / 3U;

    std::vector<NodeIndex> activeLeaves;
    CollectLeafNodes(activeLeaves);
    _stats.MaxDepthReached = 0;
    for (NodeIndex leafIndex : activeLeaves)
    {
        // leaf 分类服务于 debug color 和 benchmark 统计
        // 只遍历 active topology
        // inactive child 仍留在 node pool 中但不参与统计
        const DataOrientedRoamNode& leaf = _nodes[leafIndex];
        _stats.MaxDepthReached = std::max(_stats.MaxDepthReached, leaf.Depth);
        switch (ClassifyLeafDebug(leaf))
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
    // split path 集合是 hysteresis 的跨帧状态
    // 必须在 merge 和 split 都完成后再更新
    _previousSplitPaths = _currentSplitPaths;
    _topologyMaxDepth = _settings.MaxDepth;
    return meshData;
}

const DataOrientedRoamStats& DataOrientedRoamMeshBuilder::Stats() const
{
    return _stats;
}

DataOrientedRoamMeshBuilder::NodeIndex DataOrientedRoamMeshBuilder::AddNode(
    const TriangleDomain& domain,
    NodeIndex parent,
    int depth,
    std::uint64_t pathId)
{
    DataOrientedRoamNode node{};
    node.Domain = domain;
    node.Parent = parent;
    node.Depth = depth;
    node.PathId = pathId;
    // 所有跨节点关系只保存 NodeIndex
    // vector 扩容后也不能依赖旧元素地址
    node.CreatedBuildId = _buildSequence;
    node.ActivatedBuildId = _buildSequence;
    // geometric error 只依赖 HeightMap 和 domain
    // 节点创建后可跨帧复用
    node.GeometricError = ComputeGeometricError(domain);
    _stats.MaxDepthReached = std::max(_stats.MaxDepthReached, depth);

    _nodes.push_back(node);
    return static_cast<NodeIndex>(_nodes.size() - 1U);
}

void DataOrientedRoamMeshBuilder::ReserveNodePool()
{
    // ReserveNodePool 是 3A 的关键保障
    // 它降低扩容概率但正确性不能依赖地址稳定
    std::size_t targetCapacity = 2U + _settings.SplitBudget * 2U;
    if (_settings.MaxDepth <= ExactReserveMaxDepth)
    {
        // 完整 bintree 容量给出当前 maxDepth 的理论上界
        targetCapacity = std::max(targetCapacity, ExactBintreeNodeCapacity(_settings.MaxDepth));
    }
    else
    {
        // split budget 给不出深度很大时的完整上界
        targetCapacity = std::max(targetCapacity, LargeDepthReserveFallback);
    }

    if (_nodes.capacity() < targetCapacity)
    {
        // 取较大值可以减少 3A 节点池扩容频率
        _nodes.reserve(targetCapacity);
    }
}

void DataOrientedRoamMeshBuilder::ResetTopology()
{
    // ResetTopology 是唯一清空 node pool 的入口
    // 普通相机移动保留历史节点和 split path
    _nodes.clear();
    _previousSplitPaths.clear();
    _currentSplitPaths.clear();

    _rootA = AddNode(
        TriangleDomain{glm::vec2{0.0F, 1.0F}, glm::vec2{1.0F, 0.0F}, glm::vec2{0.0F, 0.0F}},
        InvalidNodeIndex,
        0,
        RootAPathId);
    _rootB = AddNode(
        TriangleDomain{glm::vec2{1.0F, 0.0F}, glm::vec2{0.0F, 1.0F}, glm::vec2{1.0F, 1.0F}},
        InvalidNodeIndex,
        0,
        RootBPathId);

    // 两个根三角形跨共享对角线互为 base neighbor
    // 这是 Classic ROAM 根 diamond 的起点
    _nodes[_rootA].BaseNeighbor = _rootB;
    _nodes[_rootB].BaseNeighbor = _rootA;
    _topologyMaxDepth = _settings.MaxDepth;
}

bool DataOrientedRoamMeshBuilder::NeedsTopologyReset(
    const Terrain::HeightMap& heightMap,
    float terrainSize,
    float heightScale,
    const DataOrientedRoamSettings& settings) const
{
    if (!IsValidNode(_rootA) || !IsValidNode(_rootB) || _nodes.empty())
    {
        return true;
    }

    if (_heightMap != &heightMap)
    {
        // HeightMap 指针变化意味着所有 geometric error 缓存失效
        return true;
    }

    if (settings.MaxDepth < _topologyMaxDepth)
    {
        // 降低深度时历史节点可能超过新上限
        return true;
    }

    return terrainSize != _terrainSize || heightScale != _heightScale;
}

void DataOrientedRoamMeshBuilder::RefineWithSplitQueue(NodeIndex rootA, NodeIndex rootB)
{
    // 候选队列让最显著的 screen error 先 split
    // 相比递归深挖单一路径
    // 这种策略更适合受 SplitBudget 限制的交互帧
    struct SplitCandidate
    {
        float Score{0.0F};
        std::uint64_t Sequence{0};
        NodeIndex Node{InvalidNodeIndex};
    };

    struct CandidateCompare
    {
        bool operator()(const SplitCandidate& left, const SplitCandidate& right) const
        {
            if (left.Score == right.Score)
            {
                // sequence 保证同分候选的处理顺序稳定
                // 相机静止时可以减少 mesh 抖动
                return left.Sequence > right.Sequence;
            }

            return left.Score < right.Score;
        }
    };

    std::priority_queue<SplitCandidate, std::vector<SplitCandidate>, CandidateCompare> candidates;
    std::uint64_t sequence = 0;

    const auto enqueueCandidate = [this, &candidates, &sequence](NodeIndex node) {
        // 只有 active leaf 才能进入 split 队列
        // internal node 已经由 child 接管后续细分决策
        if (!IsValidNode(node) || !IsLeaf(node) || _nodes[node].Depth >= _settings.MaxDepth)
        {
            return;
        }

        const float score = ComputeScreenErrorScore(_nodes[node]);
        if (!ShouldSplitWithScore(_nodes[node], score))
        {
            // hysteresis 区间可能沿用上一帧状态
            // 这里统一由 ShouldSplitWithScore 决定
            return;
        }

        candidates.push(SplitCandidate{score, sequence++, node});
        _stats.CandidatePeakCount = std::max(_stats.CandidatePeakCount, candidates.size());
    };

    const auto enqueueActiveLeaves = [&enqueueCandidate, this](auto&& self, NodeIndex node) -> void {
        // 持久拓扑中 root 通常已经 split
        // 每帧需要从当前 active leaf 重新评估 screen error
        if (!IsValidNode(node))
        {
            return;
        }

        if (IsLeaf(node))
        {
            enqueueCandidate(node);
            return;
        }

        self(self, _nodes[node].LeftChild);
        self(self, _nodes[node].RightChild);
    };

    enqueueActiveLeaves(enqueueActiveLeaves, rootA);
    enqueueActiveLeaves(enqueueActiveLeaves, rootB);

    while (!candidates.empty())
    {
        const SplitCandidate candidate = candidates.top();
        candidates.pop();

        const NodeIndex node = candidate.Node;
        if (!IsValidNode(node) || !IsLeaf(node))
        {
            // forced split 可能已经拆掉这个候选
            // 弹出时必须重新验证 active 状态
            continue;
        }

        const float score = ComputeScreenErrorScore(_nodes[node]);
        if (!ShouldSplitWithScore(_nodes[node], score))
        {
            // 候选分数可能因拓扑传播或相机状态变化而过期
            continue;
        }

        if (_settings.SplitBudget > 0U && _stats.SplitCount >= _settings.SplitBudget)
        {
            // 预算耗尽后停止本帧细分
            // 下一帧会从新的 active leaf 集合继续推进
            ++_stats.RejectedSplitCount;
            break;
        }

        const NodeIndex baseNeighborBeforeSplit = _nodes[node].BaseNeighbor;
        if (!SplitNode(node, SplitReason::Requested, InvalidNodeIndex))
        {
            ++_stats.RejectedSplitCount;
            continue;
        }

        enqueueCandidate(_nodes[node].LeftChild);
        enqueueCandidate(_nodes[node].RightChild);

        if (IsValidNode(baseNeighborBeforeSplit) && !IsLeaf(baseNeighborBeforeSplit))
        {
            // base neighbor 可能被约束传播提前 split
            // 新产生的 child 也要回到候选队列继续按误差评估
            enqueueCandidate(_nodes[baseNeighborBeforeSplit].LeftChild);
            enqueueCandidate(_nodes[baseNeighborBeforeSplit].RightChild);
        }
    }
}

void DataOrientedRoamMeshBuilder::MergeWithDiamondQueue()
{
    // merge candidate 从 active internal node 收集
    // 低 screen error 优先回收
    // 这样远处细节能更快回落到粗拓扑
    struct MergeCandidate
    {
        float Score{0.0F};
        NodeIndex Node{InvalidNodeIndex};
    };

    std::vector<MergeCandidate> candidates;
    const auto collectCandidates = [this, &candidates](auto&& self, NodeIndex node) -> void {
        if (!IsValidNode(node) || IsLeaf(node))
        {
            return;
        }

        if (CanMergeNode(node))
        {
            candidates.push_back(MergeCandidate{ComputeScreenErrorScore(_nodes[node]), node});
        }

        self(self, _nodes[node].LeftChild);
        self(self, _nodes[node].RightChild);
    };

    collectCandidates(collectCandidates, _rootA);
    collectCandidates(collectCandidates, _rootB);

    std::sort(
        candidates.begin(),
        candidates.end(),
        [](const MergeCandidate& left, const MergeCandidate& right) {
            return left.Score < right.Score;
        });

    for (const MergeCandidate& candidate : candidates)
    {
        if (!CanMergeNode(candidate.Node))
        {
            // 前面的 merge 可能改变后面候选的 active 状态
            continue;
        }

        if (!MergeNodeOrDiamond(candidate.Node))
        {
            ++_stats.RejectedMergeCount;
        }
    }
}

bool DataOrientedRoamMeshBuilder::SplitNode(NodeIndex node, SplitReason reason, NodeIndex forcedFrom)
{
    if (!IsValidNode(node) || !IsLeaf(node))
    {
        return false;
    }

    if (_nodes[node].Depth >= _settings.MaxDepth)
    {
        ++_stats.RejectedSplitCount;
        return false;
    }

    NodeIndex baseNeighbor = _nodes[node].BaseNeighbor;
    if (_settings.EnableLocalConstraints)
    {
        int guard = 0;
        // 非互为 base 的邻接链必须先追到合法 diamond
        // 否则单侧 split 会把一条粗边贴到多条细边上
        while (IsValidNode(baseNeighbor) &&
               baseNeighbor != forcedFrom &&
               _nodes[baseNeighbor].BaseNeighbor != node &&
               guard < _settings.MaxDepth + 2)
        {
            ++_stats.ConstraintPassCount;
            if (!SplitNode(baseNeighbor, SplitReason::ForcedByBaseNeighbor, node))
            {
                return false;
            }

            // forced split 可能改写当前节点的 base neighbor
            // 每次传播后都要刷新
            baseNeighbor = _nodes[node].BaseNeighbor;
            ++guard;
        }
    }

    if (_settings.EnableLocalConstraints && IsValidNode(baseNeighbor) && IsLeaf(baseNeighbor) && baseNeighbor != forcedFrom)
    {
        // 对侧仍是 leaf 时先补齐 base neighbor split
        // forcedFrom 防止互为 base 的两个 leaf 递归回跳
        ++_stats.ConstraintPassCount;
        if (!SplitNode(baseNeighbor, SplitReason::ForcedByBaseNeighbor, node))
        {
            return false;
        }

        baseNeighbor = _nodes[node].BaseNeighbor;
    }

    const std::uint64_t parentPathId = _nodes[node].PathId;
    if (!IsValidNode(_nodes[node].LeftChild) || !IsValidNode(_nodes[node].RightChild))
    {
        // 首次 split 创建 child
        // merge 后再次 split 时会复用同一 child index
        const TriangleDomain domain = _nodes[node].Domain;
        const int childDepth = _nodes[node].Depth + 1;
        const SplitEdge edge = ChooseBaseEdge(domain);
        const glm::vec2 midpoint = (edge.Start + edge.End) * 0.5F;

        const TriangleDomain leftDomain{edge.Apex, edge.Start, midpoint};
        const TriangleDomain rightDomain{edge.End, edge.Apex, midpoint};
        const NodeIndex leftChild = AddNode(leftDomain, node, childDepth, LeftChildPathId(parentPathId));
        const NodeIndex rightChild = AddNode(rightDomain, node, childDepth, RightChildPathId(parentPathId));
        _nodes[node].LeftChild = leftChild;
        _nodes[node].RightChild = rightChild;
    }

    DataOrientedRoamNode& parent = _nodes[node];
    parent.IsSplit = true;
    parent.SplitBuildId = _buildSequence;

    DataOrientedRoamNode& leftChild = _nodes[parent.LeftChild];
    DataOrientedRoamNode& rightChild = _nodes[parent.RightChild];
    // child 可能从历史 merge 状态复用
    // 激活前必须清空旧 neighbor
    leftChild.BaseNeighbor = InvalidNodeIndex;
    leftChild.LeftNeighbor = InvalidNodeIndex;
    leftChild.RightNeighbor = InvalidNodeIndex;
    rightChild.BaseNeighbor = InvalidNodeIndex;
    rightChild.LeftNeighbor = InvalidNodeIndex;
    rightChild.RightNeighbor = InvalidNodeIndex;
    leftChild.ActivatedBuildId = _buildSequence;
    rightChild.ActivatedBuildId = _buildSequence;
    leftChild.ActivatedByForcedSplit = reason != SplitReason::Requested;
    rightChild.ActivatedByForcedSplit = reason != SplitReason::Requested;

    LinkSplitNeighbors(node, baseNeighbor);
    _currentSplitPaths.insert(parentPathId);
    ++_stats.SplitCount;
    if (reason != SplitReason::Requested)
    {
        ++_stats.ForcedSplitCount;
    }
    return true;
}

void DataOrientedRoamMeshBuilder::LinkSplitNeighbors(NodeIndex node, NodeIndex baseNeighbor)
{
    if (!IsValidNode(node))
    {
        return;
    }

    const NodeIndex leftChild = _nodes[node].LeftChild;
    const NodeIndex rightChild = _nodes[node].RightChild;
    if (!IsValidNode(leftChild) || !IsValidNode(rightChild))
    {
        return;
    }

    _nodes[leftChild].LeftNeighbor = rightChild;
    _nodes[rightChild].RightNeighbor = leftChild;

    // child 的 base edge 分别来自父节点 left edge 和 right edge
    // 外侧 neighbor 若仍指向旧 parent
    // 必须改到 split 后共享完整边的 child
    _nodes[leftChild].BaseNeighbor = _nodes[node].LeftNeighbor;
    _nodes[rightChild].BaseNeighbor = _nodes[node].RightNeighbor;
    ReplaceNeighborReference(_nodes[node].LeftNeighbor, node, leftChild);
    ReplaceNeighborReference(_nodes[node].RightNeighbor, node, rightChild);

    if (!IsValidNode(baseNeighbor) || IsLeaf(baseNeighbor))
    {
        // 对侧没有 split 时没有完整 diamond child 可以连接
        return;
    }

    // baseNeighbor 已 split 时四个 child 共同组成 diamond
    // 这里建立跨 base edge 的左右 child 互指关系
    _nodes[leftChild].RightNeighbor = _nodes[baseNeighbor].RightChild;
    _nodes[rightChild].LeftNeighbor = _nodes[baseNeighbor].LeftChild;
    if (IsValidNode(_nodes[baseNeighbor].RightChild))
    {
        _nodes[_nodes[baseNeighbor].RightChild].LeftNeighbor = leftChild;
    }

    if (IsValidNode(_nodes[baseNeighbor].LeftChild))
    {
        _nodes[_nodes[baseNeighbor].LeftChild].RightNeighbor = rightChild;
    }
}

void DataOrientedRoamMeshBuilder::ReplaceNeighborReference(NodeIndex neighbor, NodeIndex oldNode, NodeIndex newNode)
{
    if (!IsValidNode(neighbor))
    {
        return;
    }

    if (_nodes[neighbor].BaseNeighbor == oldNode)
    {
        _nodes[neighbor].BaseNeighbor = newNode;
    }

    if (_nodes[neighbor].LeftNeighbor == oldNode)
    {
        _nodes[neighbor].LeftNeighbor = newNode;
    }

    if (_nodes[neighbor].RightNeighbor == oldNode)
    {
        _nodes[neighbor].RightNeighbor = newNode;
    }
}

bool DataOrientedRoamMeshBuilder::CanMergeNode(NodeIndex node) const
{
    // merge 只能回收两个 active leaf child
    // 如果 child 下面还有更深细分
    // 必须从更深层开始回收
    if (!IsValidNode(node) || IsLeaf(node))
    {
        return false;
    }

    const DataOrientedRoamNode& candidate = _nodes[node];
    if (!IsValidNode(candidate.LeftChild) || !IsValidNode(candidate.RightChild))
    {
        return false;
    }

    if (!IsLeaf(candidate.LeftChild) || !IsLeaf(candidate.RightChild))
    {
        return false;
    }

    if (ComputeScreenErrorScore(candidate) > _settings.MergeThreshold)
    {
        // parent 自身误差仍高时回收会造成可见 LOD 退化
        return false;
    }

    const NodeIndex baseNeighbor = candidate.BaseNeighbor;
    if (!IsValidNode(baseNeighbor) || IsLeaf(baseNeighbor))
    {
        return true;
    }

    if (_nodes[baseNeighbor].BaseNeighbor != node)
    {
        // 非互指 diamond 不能单侧 merge
        // 否则会制造 T-junction
        return false;
    }

    if (!IsValidNode(_nodes[baseNeighbor].LeftChild) || !IsValidNode(_nodes[baseNeighbor].RightChild))
    {
        return false;
    }

    if (!IsLeaf(_nodes[baseNeighbor].LeftChild) || !IsLeaf(_nodes[baseNeighbor].RightChild))
    {
        return false;
    }

    return ComputeScreenErrorScore(_nodes[baseNeighbor]) <= _settings.MergeThreshold;
}

void DataOrientedRoamMeshBuilder::MergeSingleNode(NodeIndex node)
{
    if (!IsValidNode(node) || !IsValidNode(_nodes[node].LeftChild) || !IsValidNode(_nodes[node].RightChild))
    {
        return;
    }

    const NodeIndex leftChild = _nodes[node].LeftChild;
    const NodeIndex rightChild = _nodes[node].RightChild;
    const NodeIndex newLeftNeighbor = _nodes[leftChild].BaseNeighbor;
    const NodeIndex newRightNeighbor = _nodes[rightChild].BaseNeighbor;

    // parent 重新成为 leaf 后
    // 外部 neighbor 必须从 inactive child 改回 parent
    ReplaceNeighborReference(newLeftNeighbor, leftChild, node);
    ReplaceNeighborReference(newRightNeighbor, rightChild, node);
    _nodes[node].LeftNeighbor = newLeftNeighbor;
    _nodes[node].RightNeighbor = newRightNeighbor;
    _nodes[node].IsSplit = false;
    _nodes[node].ActivatedBuildId = _buildSequence;
    _nodes[node].MergeBuildId = _buildSequence;
    _nodes[node].ActivatedByForcedSplit = false;
    ++_stats.MergeCount;
}

bool DataOrientedRoamMeshBuilder::MergeNodeOrDiamond(NodeIndex node)
{
    if (!CanMergeNode(node))
    {
        return false;
    }

    const NodeIndex baseNeighbor = _nodes[node].BaseNeighbor;
    if (IsValidNode(baseNeighbor) && !IsLeaf(baseNeighbor))
    {
        // 完整 diamond merge 要同时回收两侧 parent
        // 只回收一侧会让对侧 child 贴上粗边
        if (!CanMergeNode(baseNeighbor) || _nodes[baseNeighbor].BaseNeighbor != node)
        {
            return false;
        }

        _nodes[node].BaseNeighbor = baseNeighbor;
        _nodes[baseNeighbor].BaseNeighbor = node;
        MergeSingleNode(node);
        MergeSingleNode(baseNeighbor);
        _nodes[node].BaseNeighbor = baseNeighbor;
        _nodes[baseNeighbor].BaseNeighbor = node;
        return true;
    }

    MergeSingleNode(node);
    return true;
}

void DataOrientedRoamMeshBuilder::CollectLeafNodes(std::vector<NodeIndex>& leafNodes) const
{
    // 只能从 root 递归收集 active topology
    // node pool 中的 inactive child 不属于当前 mesh
    leafNodes.clear();
    leafNodes.reserve(_nodes.size());
    CollectLeafNodesFrom(_rootA, leafNodes);
    CollectLeafNodesFrom(_rootB, leafNodes);
}

void DataOrientedRoamMeshBuilder::CollectLeafNodesFrom(NodeIndex node, std::vector<NodeIndex>& leafNodes) const
{
    if (!IsValidNode(node))
    {
        return;
    }

    if (IsLeaf(node))
    {
        leafNodes.push_back(node);
        return;
    }

    CollectLeafNodesFrom(_nodes[node].LeftChild, leafNodes);
    CollectLeafNodesFrom(_nodes[node].RightChild, leafNodes);
}

void DataOrientedRoamMeshBuilder::CollectActiveSplitPaths()
{
    // active split path 反映 merge 后的当前拓扑
    // hysteresis 下一帧只沿这些 path 复用 split 状态
    _currentSplitPaths.clear();
    _stats.ActiveSplitCount = 0;
    CollectActiveSplitPathsFrom(_rootA);
    CollectActiveSplitPathsFrom(_rootB);
}

void DataOrientedRoamMeshBuilder::CollectActiveSplitPathsFrom(NodeIndex node)
{
    if (!IsValidNode(node) || IsLeaf(node))
    {
        return;
    }

    _currentSplitPaths.insert(_nodes[node].PathId);
    ++_stats.ActiveSplitCount;
    CollectActiveSplitPathsFrom(_nodes[node].LeftChild);
    CollectActiveSplitPathsFrom(_nodes[node].RightChild);
}

void DataOrientedRoamMeshBuilder::ValidateTopology()
{
    // validator 不修复拓扑
    // 它只把裂缝风险和邻接错误写入统计
    // smoke benchmark 用这些统计判断回归
    std::vector<NodeIndex> leafNodes;
    CollectLeafNodes(leafNodes);
    std::vector<bool> leafSet(_nodes.size(), false);
    for (NodeIndex node : leafNodes)
    {
        leafSet[node] = true;
    }

    const auto validateNeighbor = [this, &leafSet](NodeIndex owner, NodeIndex neighbor, const DomainEdge& edge) {
        // 非空 neighbor 必须是当前 active leaf
        // 还必须能在对侧找到同一条完整共享边
        if (!IsValidNode(neighbor) || !leafSet[neighbor])
        {
            return false;
        }

        for (const DomainEdge& neighborEdge : DomainEdges(_nodes[neighbor].Domain))
        {
            if (SameUndirectedEdge(edge, neighborEdge))
            {
                return _nodes[neighbor].BaseNeighbor == owner ||
                       _nodes[neighbor].LeftNeighbor == owner ||
                       _nodes[neighbor].RightNeighbor == owner;
            }
        }

        return false;
    };

    std::unordered_map<QuantizedLineKey, std::vector<long long>, QuantizedLineKeyHash> lineVertices;
    lineVertices.reserve(leafNodes.size() * 3U);
    std::vector<QuantizedEdge> quantizedEdges;
    quantizedEdges.reserve(leafNodes.size() * 3U);

    for (NodeIndex node : leafNodes)
    {
        // 每条 leaf 边都记录到量化直线索引中
        // 同一直线上的端点集合用于发现粗边内部切点
        const std::array<DomainEdge, 3> edges = DomainEdges(_nodes[node].Domain);
        for (const DomainEdge& edge : edges)
        {
            const QuantizedPoint start = QuantizePoint(edge.Start, _settings.MaxDepth);
            const QuantizedPoint end = QuantizePoint(edge.End, _settings.MaxDepth);
            const QuantizedLineKey line = MakeLineKey(start, end);
            const long long startParameter = ProjectToLineParameter(start, line);
            const long long endParameter = ProjectToLineParameter(end, line);
            quantizedEdges.push_back(QuantizedEdge{
                line,
                std::min(startParameter, endParameter),
                std::max(startParameter, endParameter),
            });

            std::vector<long long>& vertexParameters = lineVertices[line];
            vertexParameters.push_back(startParameter);
            vertexParameters.push_back(endParameter);
        }
    }

    for (auto& [line, vertexParameters] : lineVertices)
    {
        (void)line;
        // 同一条量化直线会收集多条 leaf 边端点
        // 排序去重后才能可靠做 interior 查询
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
            // 粗边内部存在其他 leaf 端点
            // 这就是典型 T-junction 风险
            ++_stats.TjunctionCount;
            ++_stats.CrackRiskCount;
        }
    }

    for (NodeIndex node : leafNodes)
    {
        const std::array<DomainEdge, 3> edges = DomainEdges(_nodes[node].Domain);

        if (IsValidNode(_nodes[node].BaseNeighbor) && !validateNeighbor(node, _nodes[node].BaseNeighbor, edges[0]))
        {
            ++_stats.InvalidNeighborCount;
        }

        if (IsValidNode(_nodes[node].RightNeighbor) && !validateNeighbor(node, _nodes[node].RightNeighbor, edges[1]))
        {
            ++_stats.InvalidNeighborCount;
        }

        if (IsValidNode(_nodes[node].LeftNeighbor) && !validateNeighbor(node, _nodes[node].LeftNeighbor, edges[2]))
        {
            ++_stats.InvalidNeighborCount;
        }
    }

    if (!IsValidNode(_rootA) ||
        !IsValidNode(_rootB) ||
        _nodes[_rootA].BaseNeighbor != _rootB ||
        _nodes[_rootB].BaseNeighbor != _rootA)
    {
        // 根 diamond 失效通常意味着 split/merge 改写了不该改的 base neighbor
        ++_stats.InvalidTopologyCount;
    }

    // 遍历整个 index pool 而不是只看 active leaf
    // inactive child 也必须保持 parent 链路正确
    for (NodeIndex nodeIndex = 0; nodeIndex < _nodes.size(); ++nodeIndex)
    {
        const DataOrientedRoamNode& node = _nodes[nodeIndex];
        if (node.IsSplit && (!IsValidNode(node.LeftChild) || !IsValidNode(node.RightChild)))
        {
            ++_stats.InvalidTopologyCount;
        }

        if (IsValidNode(node.LeftChild) && _nodes[node.LeftChild].Parent != nodeIndex)
        {
            ++_stats.InvalidTopologyCount;
        }

        if (IsValidNode(node.RightChild) && _nodes[node.RightChild].Parent != nodeIndex)
        {
            ++_stats.InvalidTopologyCount;
        }

        if (nodeIndex != _rootA && nodeIndex != _rootB && !IsValidNode(node.Parent))
        {
            // 除 root 外的节点必须能回溯到 parent
            ++_stats.InvalidTopologyCount;
        }
    }
}

void DataOrientedRoamMeshBuilder::EmitLeafTriangles(Terrain::TerrainMeshData& meshData) const
{
    // 3A 仍输出 CPU mesh
    // 后续 SoA 和并行阶段会先保持这个渲染出口稳定
    std::vector<NodeIndex> leafNodes;
    CollectLeafNodes(leafNodes);

    for (NodeIndex node : leafNodes)
    {
        EmitNode(node, meshData);
    }
}

void DataOrientedRoamMeshBuilder::EmitNode(NodeIndex node, Terrain::TerrainMeshData& meshData) const
{
    if (IsLeaf(node))
    {
        EmitDomainTriangle(_nodes[node], meshData);
    }
}

void DataOrientedRoamMeshBuilder::EmitDomainTriangle(
    const DataOrientedRoamNode& node,
    Terrain::TerrainMeshData& meshData) const
{
    const auto baseIndex = static_cast<std::uint32_t>(meshData.Vertices.size());
    const TriangleDomain& domain = node.Domain;
    const std::array<glm::vec2, 3> uvs{domain.A, domain.B, domain.C};
    const glm::vec3 debugColor = DebugColorForLeaf(node);
    const float debugHighlight = DebugHighlightForLeaf(node);

    for (const glm::vec2& uv : uvs)
    {
        // leaf 顶点从 HeightMap 即时采样
        // 这样 split 后的新中点高度和规则网格 baseline 一致
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

    // domain 绕序在递归 split 后可能和渲染面朝向相反
    // emit 阶段统一翻到正 Y
    // 后续开启 face culling 时不会出现随机消隐
    meshData.Indices.push_back(baseIndex);
    if (pointsTowardPositiveY)
    {
        meshData.Indices.push_back(baseIndex + 1U);
        meshData.Indices.push_back(baseIndex + 2U);
    }
    else
    {
        meshData.Indices.push_back(baseIndex + 2U);
        meshData.Indices.push_back(baseIndex + 1U);
    }
}

bool DataOrientedRoamMeshBuilder::ShouldSplitWithScore(
    const DataOrientedRoamNode& node,
    float screenErrorScore) const
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
        // 低于 merge 阈值时明确不 split
        // 中间区间才交给 hysteresis 保持稳定
        return false;
    }

    // hysteresis 区间沿用上一帧 split 状态
    // 避免相机轻微移动造成频繁 split / merge 抖动
    return WasSplitLastFrame(node);
}

bool DataOrientedRoamMeshBuilder::WasSplitLastFrame(const DataOrientedRoamNode& node) const
{
    return _previousSplitPaths.find(node.PathId) != _previousSplitPaths.end();
}

DataOrientedRoamMeshBuilder::LeafDebugClass DataOrientedRoamMeshBuilder::ClassifyLeafDebug(
    const DataOrientedRoamNode& node) const
{
    // Rebuilt 同时覆盖新 split child 和本帧 merge 回来的 parent
    // debug color 用它突出本帧拓扑变化区域
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

glm::vec3 DataOrientedRoamMeshBuilder::DebugColorForLeaf(const DataOrientedRoamNode& node) const
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
        // forced split 高亮 crack repair 传播路径
        if (node.ActivatedByForcedSplit)
        {
            return glm::mix(glm::vec3{0.96F, 0.34F, 0.90F}, glm::vec3{0.96F, 0.16F, 0.42F}, depthRatio);
        }

        // 普通 rebuild 使用暖色表示本帧主动拓扑变化
        return glm::mix(glm::vec3{1.0F, 0.68F, 0.15F}, glm::vec3{1.0F, 0.34F, 0.10F}, depthRatio);
    }

    return glm::vec3{0.28F, 0.34F, 0.30F};
}

float DataOrientedRoamMeshBuilder::DebugHighlightForLeaf(const DataOrientedRoamNode& node) const
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

float DataOrientedRoamMeshBuilder::ComputeGeometricError(const TriangleDomain& domain) const
{
    // 误差缓存只看 domain 对应的高度变化
    // 不依赖相机
    // 因此 node 创建后可以跨帧复用
    const float heightA = _heightMap->SampleBilinear(domain.A.x, domain.A.y);
    const float heightB = _heightMap->SampleBilinear(domain.B.x, domain.B.y);
    const float heightC = _heightMap->SampleBilinear(domain.C.x, domain.C.y);

    const auto edgeMidpointError = [this](const glm::vec2& start, const glm::vec2& end, float startHeight, float endHeight) {
        // 边中点误差能捕获边界起伏
        // 只看三角形重心会漏掉沿边的高频变化
        const glm::vec2 midpoint = (start + end) * 0.5F;
        const float midpointHeight = _heightMap->SampleBilinear(midpoint.x, midpoint.y);
        const float interpolatedHeight = (startHeight + endHeight) * 0.5F;
        return std::abs(midpointHeight - interpolatedHeight);
    };

    const glm::vec2 centroid = (domain.A + domain.B + domain.C) / 3.0F;
    const float centroidHeight = _heightMap->SampleBilinear(centroid.x, centroid.y);
    const float centroidInterpolatedHeight = (heightA + heightB + heightC) / 3.0F;

    // 取边中点和重心的最大误差
    // 平衡边界裂缝风险和三角形内部起伏
    return std::max({
        edgeMidpointError(domain.A, domain.B, heightA, heightB),
        edgeMidpointError(domain.B, domain.C, heightB, heightC),
        edgeMidpointError(domain.C, domain.A, heightC, heightA),
        std::abs(centroidHeight - centroidInterpolatedHeight),
    });
}

float DataOrientedRoamMeshBuilder::ComputeScreenErrorScore(const DataOrientedRoamNode& node) const
{
    // 当前阶段使用简化 screen error
    // 高度误差负责山体起伏
    // 边长项保证近处平坦区域仍能细分
    const glm::vec3 a = DomainToWorld(node.Domain.A);
    const glm::vec3 b = DomainToWorld(node.Domain.B);
    const glm::vec3 c = DomainToWorld(node.Domain.C);
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
    // 两项取最大值
    // 避免高频地形和近处平地互相掩盖
    return std::max(heightErrorScore, edgeLengthScore);
}

glm::vec3 DataOrientedRoamMeshBuilder::DomainToWorld(const glm::vec2& uv) const
{
    // 地形中心放在世界原点
    // 这和规则网格 builder 保持同一坐标系
    const float height = _heightMap->SampleBilinear(uv.x, uv.y);
    return glm::vec3{
        (uv.x - 0.5F) * _terrainSize,
        height * _heightScale,
        (uv.y - 0.5F) * _terrainSize,
    };
}

glm::vec3 DataOrientedRoamMeshBuilder::SampleNormal(const glm::vec2& uv) const
{
    // 法线从 HeightMap 梯度估计
    // 不依赖相邻 leaf
    // 可以避免重复顶点导致的硬边过重
    const float stepU = 1.0F / static_cast<float>(std::max(_heightMap->Width() - 1, 1));
    const float stepV = 1.0F / static_cast<float>(std::max(_heightMap->Height() - 1, 1));
    const float left = _heightMap->SampleBilinear(uv.x - stepU, uv.y);
    const float right = _heightMap->SampleBilinear(uv.x + stepU, uv.y);
    const float down = _heightMap->SampleBilinear(uv.x, uv.y - stepV);
    const float up = _heightMap->SampleBilinear(uv.x, uv.y + stepV);

    const glm::vec3 tangentX{stepU * 2.0F * _terrainSize, (right - left) * _heightScale, 0.0F};
    const glm::vec3 tangentZ{0.0F, (up - down) * _heightScale, stepV * 2.0F * _terrainSize};
    const glm::vec3 normal = glm::cross(tangentZ, tangentX);

    if (glm::dot(normal, normal) <= std::numeric_limits<float>::epsilon())
    {
        // 极端平坦或退化采样时回退竖直法线
        // shader 侧不需要处理 NaN
        return glm::vec3{0.0F, 1.0F, 0.0F};
    }

    return glm::normalize(normal);
}

bool DataOrientedRoamMeshBuilder::IsValidNode(NodeIndex node) const
{
    return node != InvalidNodeIndex && node < _nodes.size();
}

bool DataOrientedRoamMeshBuilder::IsLeaf(NodeIndex node) const
{
    return IsValidNode(node) && !_nodes[node].IsSplit;
}
} // 命名空间 ParallelRoam::Algorithms::DataOrientedRoam
