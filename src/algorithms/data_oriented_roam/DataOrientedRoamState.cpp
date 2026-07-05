#include "algorithms/data_oriented_roam/DataOrientedRoamState.h"

#include <algorithm>

namespace ParallelRoam::Algorithms::DataOrientedRoam
{
namespace
{
constexpr std::uint64_t RootAPathId = 1ULL;
constexpr std::uint64_t RootBPathId = 1ULL << 32U;
constexpr int ExactReserveMaxDepth = 20;
constexpr std::size_t LargeDepthReserveFallback = 1'000'000U;

std::size_t ExactBintreeNodeCapacity(int maxDepth)
{
    // 两棵根树都可能展开成完整二叉树
    const int safeDepth = std::clamp(maxDepth, 0, ExactReserveMaxDepth);
    // safeDepth 已限制移位范围，避免大深度溢出
    const std::size_t nodesPerRoot = (std::size_t{1} << static_cast<unsigned int>(safeDepth + 1)) - 1U;
    return nodesPerRoot * 2U;
}

void CollectLeafNodesFrom(
    const DataOrientedRoamState& state,
    DataOrientedRoamNodeIndex node,
    std::vector<DataOrientedRoamNodeIndex>& leafNodes)
{
    if (!state.IsValidNode(node))
    {
        return;
    }

    if (state.IsLeaf(node))
    {
        // active leaf 才会进入 mesh 输出和统计流程
        leafNodes.push_back(node);
        return;
    }

    CollectLeafNodesFrom(state, state.Nodes[node].LeftChild, leafNodes);
    CollectLeafNodesFrom(state, state.Nodes[node].RightChild, leafNodes);
}

void CollectActiveSplitPathsFrom(DataOrientedRoamState& state, DataOrientedRoamNodeIndex node)
{
    if (!state.IsValidNode(node) || state.IsLeaf(node))
    {
        return;
    }

    state.CurrentSplitPaths.insert(state.Nodes[node].PathId);
    ++state.Stats.ActiveSplitCount;
    CollectActiveSplitPathsFrom(state, state.Nodes[node].LeftChild);
    CollectActiveSplitPathsFrom(state, state.Nodes[node].RightChild);
}
} // 匿名命名空间

DataOrientedRoamNodeRef::operator DataOrientedRoamNodeConstRef() const
{
    return DataOrientedRoamNodeConstRef{
        Domain,
        Parent,
        LeftChild,
        RightChild,
        BaseNeighbor,
        LeftNeighbor,
        RightNeighbor,
        GeometricError,
        ScreenError,
        PathId,
        CreatedBuildId,
        ActivatedBuildId,
        SplitBuildId,
        MergeBuildId,
        Depth,
        ActivatedByForcedSplit,
        IsSplit,
    };
}

std::size_t DataOrientedRoamNodePool::size() const
{
    // Domains 是所有 SoA 数组的长度基准
    return Domains.size();
}

std::size_t DataOrientedRoamNodePool::capacity() const
{
    // 预分配容量用 domain 数组代表整个 node pool
    return Domains.capacity();
}

std::size_t DataOrientedRoamNodePool::storage_bytes() const
{
    // storage 估算按 capacity 计算，反映预分配后的内存占用
    return Domains.capacity() * sizeof(TriangleDomain) +
           // topology index arrays 是 split / merge 最频繁访问的连续字段
           Parents.capacity() * sizeof(DataOrientedRoamNodeIndex) +
           LeftChildren.capacity() * sizeof(DataOrientedRoamNodeIndex) +
           RightChildren.capacity() * sizeof(DataOrientedRoamNodeIndex) +
           BaseNeighbors.capacity() * sizeof(DataOrientedRoamNodeIndex) +
           LeftNeighbors.capacity() * sizeof(DataOrientedRoamNodeIndex) +
           RightNeighbors.capacity() * sizeof(DataOrientedRoamNodeIndex) +
           // error arrays 与拓扑 index 分离，方便后续批量评估
           GeometricErrors.capacity() * sizeof(float) +
           ScreenErrors.capacity() * sizeof(float) +
           // build id arrays 服务 debug 分类，不参与 split 队列热路径
           PathIds.capacity() * sizeof(std::uint64_t) +
           CreatedBuildIds.capacity() * sizeof(std::uint64_t) +
           ActivatedBuildIds.capacity() * sizeof(std::uint64_t) +
           SplitBuildIds.capacity() * sizeof(std::uint64_t) +
           MergeBuildIds.capacity() * sizeof(std::uint64_t) +
           // byte flags 独立存储，避免 vector<bool> bit proxy
           Depths.capacity() * sizeof(int) +
           ActivatedByForcedSplits.capacity() * sizeof(std::uint8_t) +
           IsSplits.capacity() * sizeof(std::uint8_t);
}

std::size_t DataOrientedRoamNodePool::array_count() const
{
    // array_count 用数组数量描述 SoA 字段拆分程度
    return 17U;
}

bool DataOrientedRoamNodePool::empty() const
{
    // 所有数组同步增删，因此只检查 Domains 即可
    return Domains.empty();
}

void DataOrientedRoamNodePool::clear()
{
    // clear 保留 capacity，后续 frame 可复用预分配内存
    Domains.clear();
    Parents.clear();
    LeftChildren.clear();
    RightChildren.clear();
    BaseNeighbors.clear();
    LeftNeighbors.clear();
    RightNeighbors.clear();
    GeometricErrors.clear();
    ScreenErrors.clear();
    PathIds.clear();
    CreatedBuildIds.clear();
    ActivatedBuildIds.clear();
    SplitBuildIds.clear();
    MergeBuildIds.clear();
    // 所有数组一起清空，避免同 index 指向错位字段
    Depths.clear();
    ActivatedByForcedSplits.clear();
    IsSplits.clear();
}

void DataOrientedRoamNodePool::reserve(std::size_t capacity)
{
    // topology index 数组必须和 domain 数组保持相同容量策略
    Domains.reserve(capacity);
    Parents.reserve(capacity);
    LeftChildren.reserve(capacity);
    RightChildren.reserve(capacity);
    BaseNeighbors.reserve(capacity);
    LeftNeighbors.reserve(capacity);
    RightNeighbors.reserve(capacity);
    // error 数组单独连续，可批量写入 ScreenErrors
    GeometricErrors.reserve(capacity);
    ScreenErrors.reserve(capacity);
    // build id 数组服务 debug 和本帧重建分类
    PathIds.reserve(capacity);
    CreatedBuildIds.reserve(capacity);
    ActivatedBuildIds.reserve(capacity);
    SplitBuildIds.reserve(capacity);
    MergeBuildIds.reserve(capacity);
    // depth 和 flag 分离，避免访问拓扑 index 时带入不需要的状态字节
    Depths.reserve(capacity);
    ActivatedByForcedSplits.reserve(capacity);
    IsSplits.reserve(capacity);
}

DataOrientedRoamNodeIndex DataOrientedRoamNodePool::Add(
    const TriangleDomain& domain,
    DataOrientedRoamNodeIndex parent,
    int depth,
    std::uint64_t pathId,
    std::uint64_t buildSequence,
    float geometricError)
{
    const auto index = static_cast<DataOrientedRoamNodeIndex>(Domains.size());
    // 每个 push 顺序必须完全一致，保证 index 能跨数组对齐
    Domains.push_back(domain);
    // parent 与 domain 同步写入，validator 才能遍历全池检查
    Parents.push_back(parent);
    // child 和 neighbor 默认无效，split / link pass 再填充
    LeftChildren.push_back(InvalidDataOrientedRoamNodeIndex);
    RightChildren.push_back(InvalidDataOrientedRoamNodeIndex);
    BaseNeighbors.push_back(InvalidDataOrientedRoamNodeIndex);
    LeftNeighbors.push_back(InvalidDataOrientedRoamNodeIndex);
    RightNeighbors.push_back(InvalidDataOrientedRoamNodeIndex);
    GeometricErrors.push_back(geometricError);
    // 新节点还没有经过 screen error pass
    ScreenErrors.push_back(0.0F);
    // PathId 独立于 SoA 下标，用于跨帧 hysteresis
    PathIds.push_back(pathId);
    CreatedBuildIds.push_back(buildSequence);
    ActivatedBuildIds.push_back(buildSequence);
    // split / merge id 初始为 0，只有对应 pass 会写入
    SplitBuildIds.push_back(0);
    MergeBuildIds.push_back(0);
    Depths.push_back(depth);
    // flag 使用 byte 数组，避免 vector<bool> 的代理语义干扰 pass 代码
    ActivatedByForcedSplits.push_back(0);
    IsSplits.push_back(0);
    return index;
}

DataOrientedRoamNodeRef DataOrientedRoamNodePool::operator[](DataOrientedRoamNodeIndex node)
{
    // 可写 proxy 只保存字段引用，不拷贝节点数据
    return DataOrientedRoamNodeRef{
        Domains[node],
        Parents[node],
        LeftChildren[node],
        RightChildren[node],
        BaseNeighbors[node],
        LeftNeighbors[node],
        RightNeighbors[node],
        GeometricErrors[node],
        ScreenErrors[node],
        PathIds[node],
        CreatedBuildIds[node],
        ActivatedBuildIds[node],
        SplitBuildIds[node],
        MergeBuildIds[node],
        Depths[node],
        ActivatedByForcedSplits[node],
        IsSplits[node],
    };
}

DataOrientedRoamNodeConstRef DataOrientedRoamNodePool::operator[](DataOrientedRoamNodeIndex node) const
{
    // 只读 proxy 让 scoring / validation 不需要知道数组细节
    return DataOrientedRoamNodeConstRef{
        Domains[node],
        Parents[node],
        LeftChildren[node],
        RightChildren[node],
        BaseNeighbors[node],
        LeftNeighbors[node],
        RightNeighbors[node],
        GeometricErrors[node],
        ScreenErrors[node],
        PathIds[node],
        CreatedBuildIds[node],
        ActivatedBuildIds[node],
        SplitBuildIds[node],
        MergeBuildIds[node],
        Depths[node],
        ActivatedByForcedSplits[node],
        IsSplits[node],
    };
}

bool DataOrientedRoamState::IsValidNode(DataOrientedRoamNodeIndex node) const
{
    return node != InvalidDataOrientedRoamNodeIndex && node < Nodes.size();
}

bool DataOrientedRoamState::IsLeaf(DataOrientedRoamNodeIndex node) const
{
    // IsSplits 是 byte flag，0 表示当前 active leaf
    return IsValidNode(node) && !Nodes[node].IsSplit;
}

float ElapsedMilliseconds(
    std::chrono::steady_clock::time_point start,
    std::chrono::steady_clock::time_point end)
{
    const std::chrono::duration<float, std::milli> elapsed = end - start;
    return elapsed.count();
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

DataOrientedRoamNodeIndex AddNode(
    DataOrientedRoamState& state,
    const TriangleDomain& domain,
    DataOrientedRoamNodeIndex parent,
    int depth,
    std::uint64_t pathId)
{
    // geometric error 只依赖 HeightMap 和 domain
    // 节点创建后可跨帧复用
    const float geometricError = ComputeGeometricError(state, domain);
    state.Stats.MaxDepthReached = std::max(state.Stats.MaxDepthReached, depth);

    // SoA 数组 index 是持久 node pool 的稳定节点引用
    return state.Nodes.Add(domain, parent, depth, pathId, state.BuildSequence, geometricError);
}

void ReserveNodePool(DataOrientedRoamState& state)
{
    // 预分配降低扩容概率，但正确性不能依赖地址稳定
    std::size_t targetCapacity = 2U + state.Settings.SplitBudget * 2U;
    if (state.Settings.MaxDepth <= ExactReserveMaxDepth)
    {
        // 完整 bintree 容量给出当前 maxDepth 的理论上界
        targetCapacity = std::max(targetCapacity, ExactBintreeNodeCapacity(state.Settings.MaxDepth));
    }
    else
    {
        // split budget 给不出深度很大时的完整上界
        targetCapacity = std::max(targetCapacity, LargeDepthReserveFallback);
    }

    if (state.Nodes.capacity() < targetCapacity)
    {
        // 取较大值可以减少 SoA 节点池扩容频率
        // 但所有 pass 仍必须通过 index 访问节点
        state.Nodes.reserve(targetCapacity);
    }
}

void ResetTopology(DataOrientedRoamState& state)
{
    // ResetTopology 是唯一清空 node pool 的入口
    // 普通相机移动保留历史节点和 split path
    state.Nodes.clear();
    state.PreviousSplitPaths.clear();
    state.CurrentSplitPaths.clear();

    state.RootA = AddNode(
        state,
        TriangleDomain{glm::vec2{0.0F, 1.0F}, glm::vec2{1.0F, 0.0F}, glm::vec2{0.0F, 0.0F}},
        InvalidDataOrientedRoamNodeIndex,
        0,
        RootAPathId);
    state.RootB = AddNode(
        state,
        TriangleDomain{glm::vec2{1.0F, 0.0F}, glm::vec2{0.0F, 1.0F}, glm::vec2{1.0F, 1.0F}},
        InvalidDataOrientedRoamNodeIndex,
        0,
        RootBPathId);

    // 两个 root 的 path id 位于不同区间，避免 hysteresis 键碰撞
    // 两个根三角形跨共享对角线互为 base neighbor
    // 这是 Classic ROAM 根 diamond 的起点
    state.Nodes[state.RootA].BaseNeighbor = state.RootB;
    state.Nodes[state.RootB].BaseNeighbor = state.RootA;
    state.TopologyMaxDepth = state.Settings.MaxDepth;
}

bool NeedsTopologyReset(
    const DataOrientedRoamState& state,
    const Terrain::HeightMap& heightMap,
    float terrainSize,
    float heightScale,
    const DataOrientedRoamSettings& settings)
{
    if (!state.IsValidNode(state.RootA) || !state.IsValidNode(state.RootB) || state.Nodes.empty())
    {
        // 首帧没有 root diamond，必须初始化拓扑
        return true;
    }

    if (state.HeightMap != &heightMap)
    {
        // HeightMap 指针变化意味着所有 geometric error 缓存失效
        return true;
    }

    if (settings.MaxDepth < state.TopologyMaxDepth)
    {
        // 降低深度时历史节点可能超过新上限
        return true;
    }

    return terrainSize != state.TerrainSize || heightScale != state.HeightScale;
}

void CollectLeafNodes(const DataOrientedRoamState& state, std::vector<DataOrientedRoamNodeIndex>& leafNodes)
{
    // 只能从 root 递归收集 active topology
    // node pool 中的 inactive child 不属于当前 mesh
    leafNodes.clear();
    leafNodes.reserve(state.Nodes.size());
    CollectLeafNodesFrom(state, state.RootA, leafNodes);
    CollectLeafNodesFrom(state, state.RootB, leafNodes);
}

void CollectActiveSplitPaths(DataOrientedRoamState& state)
{
    // active split path 反映 merge 后的当前拓扑
    // hysteresis 下一帧只沿这些 path 复用 split 状态
    state.CurrentSplitPaths.clear();
    state.Stats.ActiveSplitCount = 0;
    // 只从 root 走 active topology，merge 掉的路径会自然消失
    CollectActiveSplitPathsFrom(state, state.RootA);
    CollectActiveSplitPathsFrom(state, state.RootB);
}

void AccumulateLeafStats(DataOrientedRoamState& state, const Terrain::TerrainMeshData& meshData)
{
    state.Stats.NodeCount = state.Nodes.size();
    state.Stats.ReservedNodeCapacity = state.Nodes.capacity();
    state.Stats.NodeStorageBytes = state.Nodes.storage_bytes();
    state.Stats.NodeStorageArrayCount = state.Nodes.array_count();
    state.Stats.ActiveTriangleCount = meshData.Indices.size() / 3U;

    std::vector<DataOrientedRoamNodeIndex> activeLeaves;
    CollectLeafNodes(state, activeLeaves);
    state.Stats.MaxDepthReached = 0;
    for (DataOrientedRoamNodeIndex leafIndex : activeLeaves)
    {
        // inactive child 仍留在 node pool 中但不参与当前帧统计
        const DataOrientedRoamNodeConstRef leaf = state.Nodes[leafIndex];
        state.Stats.MaxDepthReached = std::max(state.Stats.MaxDepthReached, leaf.Depth);
        switch (ClassifyLeafDebug(state, leaf))
        {
        case DataOrientedRoamLeafDebugClass::Original:
            ++state.Stats.OriginalTriangleCount;
            break;
        case DataOrientedRoamLeafDebugClass::Subdivided:
            ++state.Stats.SubdividedTriangleCount;
            break;
        case DataOrientedRoamLeafDebugClass::Rebuilt:
            ++state.Stats.RebuiltTriangleCount;
            break;
        }
    }
}
} // 命名空间 ParallelRoam::Algorithms::DataOrientedRoam
