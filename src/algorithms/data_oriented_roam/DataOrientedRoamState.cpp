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
        // active leaf 才会进入 mesh emit 和统计阶段
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

bool DataOrientedRoamState::IsValidNode(DataOrientedRoamNodeIndex node) const
{
    return node != InvalidDataOrientedRoamNodeIndex && node < Nodes.size();
}

bool DataOrientedRoamState::IsLeaf(DataOrientedRoamNodeIndex node) const
{
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
    DataOrientedRoamNode node{};
    node.Domain = domain;
    node.Parent = parent;
    node.Depth = depth;
    node.PathId = pathId;
    // 所有跨节点关系只保存 NodeIndex
    // vector 扩容后也不能依赖旧元素地址
    node.CreatedBuildId = state.BuildSequence;
    node.ActivatedBuildId = state.BuildSequence;
    // geometric error 只依赖 HeightMap 和 domain
    // 节点创建后可跨帧复用
    node.GeometricError = ComputeGeometricError(state, domain);
    state.Stats.MaxDepthReached = std::max(state.Stats.MaxDepthReached, depth);

    state.Nodes.push_back(node);
    // vector index 是 3A 版本唯一稳定的节点引用
    return static_cast<DataOrientedRoamNodeIndex>(state.Nodes.size() - 1U);
}

void ReserveNodePool(DataOrientedRoamState& state)
{
    // ReserveNodePool 是 3A 的关键保障
    // 它降低扩容概率但正确性不能依赖地址稳定
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
        // 取较大值可以减少 3A 节点池扩容频率
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
    state.Stats.ActiveTriangleCount = meshData.Indices.size() / 3U;

    std::vector<DataOrientedRoamNodeIndex> activeLeaves;
    CollectLeafNodes(state, activeLeaves);
    state.Stats.MaxDepthReached = 0;
    for (DataOrientedRoamNodeIndex leafIndex : activeLeaves)
    {
        // inactive child 仍留在 node pool 中但不参与当前帧统计
        const DataOrientedRoamNode& leaf = state.Nodes[leafIndex];
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
