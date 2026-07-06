#include "algorithms/data_oriented_roam/DataOrientedRoamParallel.h"
#include "algorithms/data_oriented_roam/DataOrientedRoamState.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <queue>
#include <thread>
#include <vector>

namespace ParallelRoam::Algorithms::DataOrientedRoam
{
namespace
{
constexpr std::size_t MaxTopologyCommitWorkerCount = 8;
// 候选太少时保留串行提交，避免调度成本超过收益
constexpr std::size_t MinParallelCommitCandidateCount = 32;

/// <summary>
/// 并发 worker 的本地提交计数，join 后再合并回全局 stats
/// </summary>
struct TopologyCommitCounters
{
    // split 和 forced split 分开保留，便于维持原有统计语义
    std::size_t SplitCount{0};
    std::size_t ForcedSplitCount{0};
    std::size_t RejectedSplitCount{0};
    // 约束传播理论上不会出现在并发 split，但计数器仍保留防御口径
    std::size_t ConstraintPassCount{0};
    std::size_t MergeCount{0};
};

/// <summary>
/// 并发 split 成功后返回给串行 priority queue 的增量节点
/// </summary>
struct CommittedSplit
{
    // Node 已经从 leaf 变为 internal
    DataOrientedRoamNodeIndex Node{InvalidDataOrientedRoamNodeIndex};
    // split 前的 base neighbor 用于重新评价 diamond 对侧 child
    DataOrientedRoamNodeIndex BaseNeighborBeforeSplit{InvalidDataOrientedRoamNodeIndex};
};

struct SplitEdge
{
    // Start 和 End 是 base edge 两端
    glm::vec2 Start{0.0F};
    glm::vec2 End{0.0F};
    // Apex 是 base edge 对侧顶点
    glm::vec2 Apex{0.0F};
};

DataOrientedRoamChunkId InteriorChunkIdForNode(const DataOrientedRoamState& state, DataOrientedRoamNodeIndex node)
{
    if (!state.IsValidNode(node))
    {
        // invalid node 不能被分配给任何 chunk
        return InvalidDataOrientedRoamChunkId;
    }

    // node 创建时已缓存 chunk 归属
    return state.Nodes[node].InteriorChunkId;
}

bool NodeBelongsToChunk(
    const DataOrientedRoamState& state,
    DataOrientedRoamNodeIndex node,
    DataOrientedRoamChunkId chunkId)
{
    if (!state.IsValidNode(node))
    {
        // invalid neighbor 不会被写入，视作不阻塞 chunk 内提交
        return true;
    }

    return InteriorChunkIdForNode(state, node) == chunkId;
}

std::size_t ResolveTopologyCommitWorkerCount(
    const DataOrientedRoamState& state,
    std::size_t candidateCount,
    std::size_t nonEmptyChunkCount)
{
    if (candidateCount < MinParallelCommitCandidateCount || nonEmptyChunkCount < 2U)
    {
        // 单 chunk 或小批量没有并发提交价值
        return 1U;
    }

    if (state.Settings.ErrorEvaluationWorkerCount == 1U)
    {
        // 拓扑提交沿用 worker 设置，避免新增 UI 参数
        return 1U;
    }

    std::size_t requestedWorkerCount = state.Settings.ErrorEvaluationWorkerCount;
    if (requestedWorkerCount == 0U)
    {
        // 自动模式保守封顶，避免 topology commit 抢占过多线程
        const unsigned int hardwareWorkerCount = std::thread::hardware_concurrency();
        requestedWorkerCount = hardwareWorkerCount == 0U ? 1U : static_cast<std::size_t>(hardwareWorkerCount);
        requestedWorkerCount = std::min(requestedWorkerCount, MaxTopologyCommitWorkerCount);
    }

    return std::clamp(requestedWorkerCount, std::size_t{1}, nonEmptyChunkCount);
}

void MergeCountersIntoStats(DataOrientedRoamState& state, const TopologyCommitCounters& counters)
{
    // worker 本地计数在主线程合并，避免 stats 字段数据竞争
    state.Stats.SplitCount += counters.SplitCount;
    state.Stats.ForcedSplitCount += counters.ForcedSplitCount;
    state.Stats.RejectedSplitCount += counters.RejectedSplitCount;
    state.Stats.ConstraintPassCount += counters.ConstraintPassCount;
    state.Stats.MergeCount += counters.MergeCount;
}

void RecordConstraintPass(DataOrientedRoamState& state, TopologyCommitCounters* counters)
{
    if (counters != nullptr)
    {
        // 并发路径只写本地计数器
        ++counters->ConstraintPassCount;
        return;
    }

    ++state.Stats.ConstraintPassCount;
}

void RecordRejectedSplit(DataOrientedRoamState& state, TopologyCommitCounters* counters)
{
    if (counters != nullptr)
    {
        // 并发路径延迟合并 rejected split
        ++counters->RejectedSplitCount;
        return;
    }

    ++state.Stats.RejectedSplitCount;
}

void RecordSplit(
    DataOrientedRoamState& state,
    std::uint64_t parentPathId,
    DataOrientedRoamSplitReason reason,
    TopologyCommitCounters* counters)
{
    if (counters != nullptr)
    {
        // 并发 split 不直接写 CurrentSplitPaths
        ++counters->SplitCount;
        if (reason != DataOrientedRoamSplitReason::Requested)
        {
            ++counters->ForcedSplitCount;
        }
        return;
    }

    state.CurrentSplitPaths.insert(parentPathId);
    ++state.Stats.SplitCount;
    if (reason != DataOrientedRoamSplitReason::Requested)
    {
        ++state.Stats.ForcedSplitCount;
    }
}

void RecordMerge(DataOrientedRoamState& state, TopologyCommitCounters* counters)
{
    if (counters != nullptr)
    {
        // 并发 merge 只累积本地成功次数
        ++counters->MergeCount;
        return;
    }

    ++state.Stats.MergeCount;
}

SplitEdge ChooseBaseEdge(const TriangleDomain& domain)
{
    // Classic ROAM 固定把 A-B 作为 base edge
    // DOD 版必须保留这个约定才能和 Classic benchmark 对齐
    return SplitEdge{domain.A, domain.B, domain.C};
}

void ReplaceNeighborReference(
    DataOrientedRoamState& state,
    DataOrientedRoamNodeIndex neighbor,
    DataOrientedRoamNodeIndex oldNode,
    DataOrientedRoamNodeIndex newNode)
{
    if (!state.IsValidNode(neighbor))
    {
        return;
    }

    if (state.Nodes[neighbor].BaseNeighbor == oldNode)
    {
        // base edge 对应旧 parent 时改到 split 后 child
        state.Nodes[neighbor].BaseNeighbor = newNode;
    }

    if (state.Nodes[neighbor].LeftNeighbor == oldNode)
    {
        // left edge 引用旧 parent 时同步替换
        state.Nodes[neighbor].LeftNeighbor = newNode;
    }

    if (state.Nodes[neighbor].RightNeighbor == oldNode)
    {
        // right edge 引用旧 parent 时同步替换
        state.Nodes[neighbor].RightNeighbor = newNode;
    }
}

void LinkSplitNeighbors(
    DataOrientedRoamState& state,
    DataOrientedRoamNodeIndex node,
    DataOrientedRoamNodeIndex baseNeighbor)
{
    if (!state.IsValidNode(node))
    {
        return;
    }

    const DataOrientedRoamNodeIndex leftChild = state.Nodes[node].LeftChild;
    const DataOrientedRoamNodeIndex rightChild = state.Nodes[node].RightChild;
    if (!state.IsValidNode(leftChild) || !state.IsValidNode(rightChild))
    {
        return;
    }

    state.Nodes[leftChild].LeftNeighbor = rightChild;
    state.Nodes[rightChild].RightNeighbor = leftChild;
    // 两个 child 之间共享 split 中线

    // child 的 base edge 分别来自父节点 left edge 和 right edge
    // 外侧 neighbor 若仍指向旧 parent，必须改到共享完整边的 child
    state.Nodes[leftChild].BaseNeighbor = state.Nodes[node].LeftNeighbor;
    state.Nodes[rightChild].BaseNeighbor = state.Nodes[node].RightNeighbor;
    ReplaceNeighborReference(state, state.Nodes[node].LeftNeighbor, node, leftChild);
    ReplaceNeighborReference(state, state.Nodes[node].RightNeighbor, node, rightChild);

    if (!state.IsValidNode(baseNeighbor) || state.IsLeaf(baseNeighbor))
    {
        // 对侧没有 split 时没有完整 diamond child 可以连接
        return;
    }

    // baseNeighbor 已 split 时四个 child 共同组成 diamond
    state.Nodes[leftChild].RightNeighbor = state.Nodes[baseNeighbor].RightChild;
    state.Nodes[rightChild].LeftNeighbor = state.Nodes[baseNeighbor].LeftChild;
    if (state.IsValidNode(state.Nodes[baseNeighbor].RightChild))
    {
        state.Nodes[state.Nodes[baseNeighbor].RightChild].LeftNeighbor = leftChild;
    }

    if (state.IsValidNode(state.Nodes[baseNeighbor].LeftChild))
    {
        state.Nodes[state.Nodes[baseNeighbor].LeftChild].RightNeighbor = rightChild;
    }
}

bool SplitNode(
    DataOrientedRoamState& state,
    DataOrientedRoamNodeIndex node,
    DataOrientedRoamSplitReason reason,
    DataOrientedRoamNodeIndex forcedFrom,
    TopologyCommitCounters* counters = nullptr)
{
    if (!state.IsValidNode(node) || !state.IsLeaf(node))
    {
        // internal node 已经由 child 接管细分决策
        return false;
    }

    if (state.Nodes[node].Depth >= state.Settings.MaxDepth)
    {
        // maxDepth 是硬限制，不进入约束传播
        RecordRejectedSplit(state, counters);
        return false;
    }

    DataOrientedRoamNodeIndex baseNeighbor = state.Nodes[node].BaseNeighbor;
    if (state.Settings.EnableLocalConstraints)
    {
        // local constraint 只在设置开启时传播 forced split
        int guard = 0;
        // 非互为 base 的邻接链必须先追到合法 diamond
        // 否则单侧 split 会把一条粗边贴到多条细边上
        while (state.IsValidNode(baseNeighbor) &&
               baseNeighbor != forcedFrom &&
               state.Nodes[baseNeighbor].BaseNeighbor != node &&
               guard < state.Settings.MaxDepth + 2)
        {
            RecordConstraintPass(state, counters);
            if (!SplitNode(state, baseNeighbor, DataOrientedRoamSplitReason::ForcedByBaseNeighbor, node, counters))
            {
                // 约束传播失败时当前 split 也必须失败
                return false;
            }

            baseNeighbor = state.Nodes[node].BaseNeighbor;
            ++guard;
        }
    }

    if (state.Settings.EnableLocalConstraints &&
        state.IsValidNode(baseNeighbor) &&
        state.IsLeaf(baseNeighbor) &&
        baseNeighbor != forcedFrom)
    {
        // 对侧仍是 leaf 时先补齐 base neighbor split
        // forcedFrom 防止互为 base 的两个 leaf 递归回跳
        RecordConstraintPass(state, counters);
        if (!SplitNode(state, baseNeighbor, DataOrientedRoamSplitReason::ForcedByBaseNeighbor, node, counters))
        {
            // 对侧 leaf 无法补齐时不能单侧 split
            return false;
        }

        baseNeighbor = state.Nodes[node].BaseNeighbor;
    }

    const std::uint64_t parentPathId = state.Nodes[node].PathId;
    if (!state.IsValidNode(state.Nodes[node].LeftChild) || !state.IsValidNode(state.Nodes[node].RightChild))
    {
        // 首次 split 创建 child，merge 后再次 split 时复用同一 child index
        const TriangleDomain domain = state.Nodes[node].Domain;
        const int childDepth = state.Nodes[node].Depth + 1;
        const SplitEdge edge = ChooseBaseEdge(domain);
        const glm::vec2 midpoint = (edge.Start + edge.End) * 0.5F;

        const TriangleDomain leftDomain{edge.Apex, edge.Start, midpoint};
        const TriangleDomain rightDomain{edge.End, edge.Apex, midpoint};
        const DataOrientedRoamNodeIndex leftChild =
            AddNode(state, leftDomain, node, childDepth, LeftChildPathId(parentPathId));
        const DataOrientedRoamNodeIndex rightChild =
            AddNode(state, rightDomain, node, childDepth, RightChildPathId(parentPathId));
        state.Nodes[node].LeftChild = leftChild;
        state.Nodes[node].RightChild = rightChild;
    }

    auto parent = state.Nodes[node];
    // parent 留在 node pool 中，但不再是 active leaf
    parent.IsSplit = true;
    parent.SplitBuildId = state.BuildSequence;

    auto leftChild = state.Nodes[parent.LeftChild];
    auto rightChild = state.Nodes[parent.RightChild];
    // child 可能从历史 merge 状态复用，激活前必须清空旧 neighbor
    leftChild.BaseNeighbor = InvalidDataOrientedRoamNodeIndex;
    leftChild.LeftNeighbor = InvalidDataOrientedRoamNodeIndex;
    leftChild.RightNeighbor = InvalidDataOrientedRoamNodeIndex;
    rightChild.BaseNeighbor = InvalidDataOrientedRoamNodeIndex;
    rightChild.LeftNeighbor = InvalidDataOrientedRoamNodeIndex;
    rightChild.RightNeighbor = InvalidDataOrientedRoamNodeIndex;
    leftChild.ActivatedBuildId = state.BuildSequence;
    rightChild.ActivatedBuildId = state.BuildSequence;
    leftChild.ActivatedByForcedSplit = reason != DataOrientedRoamSplitReason::Requested;
    rightChild.ActivatedByForcedSplit = reason != DataOrientedRoamSplitReason::Requested;

    LinkSplitNeighbors(state, node, baseNeighbor);
    // 串行路径会记录 path，最终仍由 CollectActiveSplitPaths 重建一次
    RecordSplit(state, parentPathId, reason, counters);
    return true;
}

void MergeSingleNode(
    DataOrientedRoamState& state,
    DataOrientedRoamNodeIndex node,
    TopologyCommitCounters* counters = nullptr)
{
    if (!state.IsValidNode(node) ||
        !state.IsValidNode(state.Nodes[node].LeftChild) ||
        !state.IsValidNode(state.Nodes[node].RightChild))
    {
        return;
    }

    const DataOrientedRoamNodeIndex leftChild = state.Nodes[node].LeftChild;
    const DataOrientedRoamNodeIndex rightChild = state.Nodes[node].RightChild;
    const DataOrientedRoamNodeIndex newLeftNeighbor = state.Nodes[leftChild].BaseNeighbor;
    const DataOrientedRoamNodeIndex newRightNeighbor = state.Nodes[rightChild].BaseNeighbor;

    // parent 重新成为 leaf 后，外部 neighbor 必须从 inactive child 改回 parent
    ReplaceNeighborReference(state, newLeftNeighbor, leftChild, node);
    ReplaceNeighborReference(state, newRightNeighbor, rightChild, node);
    state.Nodes[node].LeftNeighbor = newLeftNeighbor;
    state.Nodes[node].RightNeighbor = newRightNeighbor;
    state.Nodes[node].IsSplit = false;
    state.Nodes[node].ActivatedBuildId = state.BuildSequence;
    state.Nodes[node].MergeBuildId = state.BuildSequence;
    state.Nodes[node].ActivatedByForcedSplit = false;
    RecordMerge(state, counters);
}

bool MergeNodeOrDiamond(
    DataOrientedRoamState& state,
    DataOrientedRoamNodeIndex node,
    TopologyCommitCounters* counters = nullptr)
{
    if (!CanMergeNode(state, node))
    {
        return false;
    }

    const DataOrientedRoamNodeIndex baseNeighbor = state.Nodes[node].BaseNeighbor;
    if (state.IsValidNode(baseNeighbor) && !state.IsLeaf(baseNeighbor))
    {
        // 完整 diamond merge 要同时回收两侧 parent
        // 只回收一侧会让对侧 child 贴上粗边
        if (!CanMergeNode(state, baseNeighbor) || state.Nodes[baseNeighbor].BaseNeighbor != node)
        {
            return false;
        }

        state.Nodes[node].BaseNeighbor = baseNeighbor;
        state.Nodes[baseNeighbor].BaseNeighbor = node;
        // MergeSingleNode 不改 baseNeighbor，互指关系需要前后显式保持
        MergeSingleNode(state, node, counters);
        MergeSingleNode(state, baseNeighbor, counters);
        state.Nodes[node].BaseNeighbor = baseNeighbor;
        state.Nodes[baseNeighbor].BaseNeighbor = node;
        return true;
    }

    MergeSingleNode(state, node, counters);
    return true;
}

bool HasReusableChildren(const DataOrientedRoamState& state, DataOrientedRoamNodeIndex node)
{
    // 并发 split 第一版不做 node pool 分配，只复用历史 child
    return state.IsValidNode(node) &&
           state.IsValidNode(state.Nodes[node].LeftChild) &&
           state.IsValidNode(state.Nodes[node].RightChild);
}

bool SplitWouldNeedForcedNeighbor(const DataOrientedRoamState& state, DataOrientedRoamNodeIndex node)
{
    if (!state.Settings.EnableLocalConstraints)
    {
        // 关闭约束时不会递归触发 base neighbor split
        return false;
    }

    const DataOrientedRoamNodeIndex baseNeighbor = state.Nodes[node].BaseNeighbor;
    if (!state.IsValidNode(baseNeighbor))
    {
        // 地形边界没有对侧三角形
        return false;
    }

    if (state.IsLeaf(baseNeighbor))
    {
        // leaf base neighbor 会触发 forced split，必须串行处理
        return true;
    }

    // 非互指 diamond 需要沿 neighbor 链修复，也交给串行路径
    return state.Nodes[baseNeighbor].BaseNeighbor != node;
}

DataOrientedRoamChunkId SafeInteriorSplitChunkId(const DataOrientedRoamState& state, DataOrientedRoamNodeIndex node)
{
    if (!state.IsValidNode(node) ||
        !state.IsLeaf(node) ||
        state.Nodes[node].Depth >= state.Settings.MaxDepth ||
        !HasReusableChildren(state, node) ||
        SplitWouldNeedForcedNeighbor(state, node))
    {
        // 任一条件不满足都会回退到原有串行 split queue
        return InvalidDataOrientedRoamChunkId;
    }

    const DataOrientedRoamChunkId chunkId = InteriorChunkIdForNode(state, node);
    if (chunkId == InvalidDataOrientedRoamChunkId)
    {
        // 跨 chunk 三角形属于 boundary candidate
        return InvalidDataOrientedRoamChunkId;
    }

    const DataOrientedRoamNodeConstRef candidate = state.Nodes[node];
    // split 会写 parent、两个 child 和左右外侧 neighbor
    if (!NodeBelongsToChunk(state, candidate.LeftChild, chunkId) ||
        !NodeBelongsToChunk(state, candidate.RightChild, chunkId) ||
        !NodeBelongsToChunk(state, candidate.LeftNeighbor, chunkId) ||
        !NodeBelongsToChunk(state, candidate.RightNeighbor, chunkId))
    {
        return InvalidDataOrientedRoamChunkId;
    }

    const DataOrientedRoamNodeIndex baseNeighbor = candidate.BaseNeighbor;
    if (state.IsValidNode(baseNeighbor) && !state.IsLeaf(baseNeighbor))
    {
        // diamond 对侧已 split 时还会写入对侧 child 的 neighbor
        if (!NodeBelongsToChunk(state, baseNeighbor, chunkId) ||
            !NodeBelongsToChunk(state, state.Nodes[baseNeighbor].LeftChild, chunkId) ||
            !NodeBelongsToChunk(state, state.Nodes[baseNeighbor].RightChild, chunkId))
        {
            return InvalidDataOrientedRoamChunkId;
        }
    }

    return chunkId;
}

bool HasMergeReadyChildren(const DataOrientedRoamState& state, DataOrientedRoamNodeIndex node)
{
    if (!state.IsValidNode(node) || state.IsLeaf(node))
    {
        // merge 只作用于 active internal node
        return false;
    }

    const DataOrientedRoamNodeConstRef candidate = state.Nodes[node];
    // 分桶时只检查拓扑形状，score 校验留到提交前
    return state.IsValidNode(candidate.LeftChild) &&
           state.IsValidNode(candidate.RightChild) &&
           state.IsLeaf(candidate.LeftChild) &&
           state.IsLeaf(candidate.RightChild);
}

bool HasMergeReadyDiamond(const DataOrientedRoamState& state, DataOrientedRoamNodeIndex node)
{
    const DataOrientedRoamNodeIndex baseNeighbor = state.Nodes[node].BaseNeighbor;
    if (!state.IsValidNode(baseNeighbor) || state.IsLeaf(baseNeighbor))
    {
        // 没有对侧 internal diamond 时可以单侧 merge
        return true;
    }

    // 对侧 diamond 也必须是可回收的两片 leaf
    return state.Nodes[baseNeighbor].BaseNeighbor == node && HasMergeReadyChildren(state, baseNeighbor);
}

DataOrientedRoamChunkId SafeInteriorMergeChunkId(
    const DataOrientedRoamState& state,
    DataOrientedRoamNodeIndex node,
    bool validateMergeScore)
{
    if (!HasMergeReadyChildren(state, node) || !HasMergeReadyDiamond(state, node))
    {
        // merge 前置拓扑不满足时不进入任何提交队列
        return InvalidDataOrientedRoamChunkId;
    }

    if (validateMergeScore && !CanMergeNode(state, node))
    {
        // worker 真正提交前再做完整 score 校验
        return InvalidDataOrientedRoamChunkId;
    }

    const DataOrientedRoamChunkId chunkId = InteriorChunkIdForNode(state, node);
    if (chunkId == InvalidDataOrientedRoamChunkId)
    {
        // parent 自身跨 chunk 时不能并发回收
        return InvalidDataOrientedRoamChunkId;
    }

    const DataOrientedRoamNodeConstRef candidate = state.Nodes[node];
    // MergeSingleNode 会写两个 child 的 base neighbor 所指向的外侧节点
    if (!NodeBelongsToChunk(state, candidate.LeftChild, chunkId) ||
        !NodeBelongsToChunk(state, candidate.RightChild, chunkId) ||
        !NodeBelongsToChunk(state, state.Nodes[candidate.LeftChild].BaseNeighbor, chunkId) ||
        !NodeBelongsToChunk(state, state.Nodes[candidate.RightChild].BaseNeighbor, chunkId))
    {
        return InvalidDataOrientedRoamChunkId;
    }

    const DataOrientedRoamNodeIndex baseNeighbor = candidate.BaseNeighbor;
    if (state.IsValidNode(baseNeighbor) && !state.IsLeaf(baseNeighbor))
    {
        // diamond merge 会同时回收 base neighbor 一侧
        if ((validateMergeScore && !CanMergeNode(state, baseNeighbor)) ||
            !NodeBelongsToChunk(state, baseNeighbor, chunkId) ||
            !NodeBelongsToChunk(state, state.Nodes[baseNeighbor].LeftChild, chunkId) ||
            !NodeBelongsToChunk(state, state.Nodes[baseNeighbor].RightChild, chunkId) ||
            !NodeBelongsToChunk(state, state.Nodes[state.Nodes[baseNeighbor].LeftChild].BaseNeighbor, chunkId) ||
            !NodeBelongsToChunk(state, state.Nodes[state.Nodes[baseNeighbor].RightChild].BaseNeighbor, chunkId))
        {
            return InvalidDataOrientedRoamChunkId;
        }
    }

    return chunkId;
}

std::vector<std::vector<DataOrientedRoamSplitCandidate>> BuildInteriorSplitChunks(
    DataOrientedRoamState& state,
    const std::vector<DataOrientedRoamSplitCandidate>& candidates)
{
    // 先按原 priority queue 口径排序，再筛选可并发提交的安全候选
    std::vector<DataOrientedRoamSplitCandidate> sortedCandidates = candidates;
    std::sort(
        sortedCandidates.begin(),
        sortedCandidates.end(),
        [](const DataOrientedRoamSplitCandidate& left, const DataOrientedRoamSplitCandidate& right) {
            if (left.Score == right.Score)
            {
                return left.Sequence < right.Sequence;
            }

            return left.Score > right.Score;
        });

    std::vector<std::vector<DataOrientedRoamSplitCandidate>> chunks(
        static_cast<std::size_t>(
            DataOrientedRoamTopologyChunkGridSize * DataOrientedRoamTopologyChunkGridSize));
    for (const DataOrientedRoamSplitCandidate& candidate : sortedCandidates)
    {
        const DataOrientedRoamChunkId chunkId = SafeInteriorSplitChunkId(state, candidate.Node);
        if (chunkId == InvalidDataOrientedRoamChunkId)
        {
            // boundary candidate 保留给串行 queue
            ++state.Stats.BoundarySplitCandidateCount;
            continue;
        }

        // chunk 下标即并发任务的 ownership
        chunks[chunkId].push_back(candidate);
        ++state.Stats.InteriorSplitCandidateCount;
    }

    return chunks;
}

std::vector<std::vector<DataOrientedRoamMergeCandidate>> BuildInteriorMergeChunks(
    DataOrientedRoamState& state,
    const std::vector<DataOrientedRoamMergeCandidate>& candidates)
{
    std::vector<std::vector<DataOrientedRoamMergeCandidate>> chunks(
        static_cast<std::size_t>(
            DataOrientedRoamTopologyChunkGridSize * DataOrientedRoamTopologyChunkGridSize));

    // merge 不受 split 队列影响，所有安全 interior 候选都可先分桶
    for (const DataOrientedRoamMergeCandidate& candidate : candidates)
    {
        // merge candidate 已按 score 排好序，chunk 内保留这个顺序
        const DataOrientedRoamChunkId chunkId = SafeInteriorMergeChunkId(state, candidate.Node, false);
        if (chunkId == InvalidDataOrientedRoamChunkId)
        {
            // 跨 chunk diamond 仍由串行路径提交
            ++state.Stats.BoundaryMergeCandidateCount;
            continue;
        }

        // 同一 chunk 内由同一个 worker 顺序提交
        chunks[chunkId].push_back(candidate);
        ++state.Stats.InteriorMergeCandidateCount;
    }

    return chunks;
}

std::size_t CountNonEmptyChunks(auto& chunks)
{
    std::size_t nonEmptyChunkCount = 0U;
    for (const auto& chunk : chunks)
    {
        if (!chunk.empty())
        {
            // 非空 chunk 数决定最多能并行多少个独立任务
            ++nonEmptyChunkCount;
        }
    }

    return nonEmptyChunkCount;
}

std::size_t CountChunkCandidates(auto& chunks)
{
    std::size_t candidateCount = 0U;
    for (const auto& chunk : chunks)
    {
        // 只统计 interior candidate，不含串行 boundary 回退
        candidateCount += chunk.size();
    }

    return candidateCount;
}

std::vector<CommittedSplit> CommitInteriorSplitChunks(
    DataOrientedRoamState& state,
    std::vector<std::vector<DataOrientedRoamSplitCandidate>>& chunks)
{
    std::vector<CommittedSplit> committedSplits;
    const std::size_t nonEmptyChunkCount = CountNonEmptyChunks(chunks);
    const std::size_t candidateCount = CountChunkCandidates(chunks);
    const std::size_t workerCount = ResolveTopologyCommitWorkerCount(state, candidateCount, nonEmptyChunkCount);
    state.Stats.TopologyCommitWorkerCount = std::max(state.Stats.TopologyCommitWorkerCount, workerCount);

    if (workerCount <= 1U)
    {
        // worker 不足时不预提交 split，保持原串行 queue 语义
        return committedSplits;
    }

    std::vector<TopologyCommitCounters> localCounters(workerCount);
    std::vector<std::vector<CommittedSplit>> localCommittedSplits(workerCount);

    RunDataOrientedRoamWorkers(state, workerCount, [&](std::size_t workerIndex) {
        // 每个 chunk 只会被一个 worker 访问
        for (std::size_t chunkIndex = workerIndex; chunkIndex < chunks.size(); chunkIndex += workerCount)
        {
            for (const DataOrientedRoamSplitCandidate& candidate : chunks[chunkIndex])
            {
                const DataOrientedRoamNodeIndex node = candidate.Node;
                // 同 chunk 前序提交后需要重新确认 cached chunk ownership
                const DataOrientedRoamChunkId chunkId = SafeInteriorSplitChunkId(state, node);
                if (chunkId != chunkIndex)
                {
                    // 同 chunk 前序提交可能让候选不再安全
                    continue;
                }

                const DataOrientedRoamNodeIndex baseNeighborBeforeSplit = state.Nodes[node].BaseNeighbor;
                // 并发 split 只允许不分配新 node 的安全候选
                if (SplitNode(
                        state,
                        node,
                        DataOrientedRoamSplitReason::Requested,
                        InvalidDataOrientedRoamNodeIndex,
                        &localCounters[workerIndex]))
                {
                    // child 会在主线程重新入队，保持级联细分
                    localCommittedSplits[workerIndex].push_back(CommittedSplit{node, baseNeighborBeforeSplit});
                }
            }
        }
    });

    std::size_t totalCommittedCount = 0U;
    for (const TopologyCommitCounters& counters : localCounters)
    {
        // 所有全局 stats 更新集中在主线程完成
        MergeCountersIntoStats(state, counters);
        totalCommittedCount += counters.SplitCount;
    }

    for (const std::vector<CommittedSplit>& localSplits : localCommittedSplits)
    {
        // 合并顺序只影响后续同分 sequence，不影响拓扑正确性
        committedSplits.insert(committedSplits.end(), localSplits.begin(), localSplits.end());
    }

    state.Stats.ParallelSplitCommitCount += totalCommittedCount;
    return committedSplits;
}

void CommitInteriorMergeChunks(
    DataOrientedRoamState& state,
    std::vector<std::vector<DataOrientedRoamMergeCandidate>>& chunks)
{
    const std::size_t nonEmptyChunkCount = CountNonEmptyChunks(chunks);
    const std::size_t candidateCount = CountChunkCandidates(chunks);
    const std::size_t workerCount = ResolveTopologyCommitWorkerCount(state, candidateCount, nonEmptyChunkCount);
    state.Stats.TopologyCommitWorkerCount = std::max(state.Stats.TopologyCommitWorkerCount, workerCount);

    if (workerCount <= 1U)
    {
        // 小批量 merge 直接交给原串行路径
        return;
    }

    std::vector<TopologyCommitCounters> localCounters(workerCount);

    RunDataOrientedRoamWorkers(state, workerCount, [&](std::size_t workerIndex) {
        // chunk ownership 保证不同 worker 不写同一组 neighbor
        for (std::size_t chunkIndex = workerIndex; chunkIndex < chunks.size(); chunkIndex += workerCount)
        {
            for (const DataOrientedRoamMergeCandidate& candidate : chunks[chunkIndex])
            {
                const DataOrientedRoamNodeIndex node = candidate.Node;
                const DataOrientedRoamChunkId chunkId = SafeInteriorMergeChunkId(state, node, true);
                if (chunkId != chunkIndex)
                {
                    // 前序 merge 可能已经改变 diamond 结构
                    continue;
                }

                // 真正提交前仍复用原 diamond merge 逻辑
                MergeNodeOrDiamond(state, node, &localCounters[workerIndex]);
            }
        }
    });

    std::size_t totalCommittedCount = 0U;
    for (const TopologyCommitCounters& counters : localCounters)
    {
        // merge 成功次数由 worker 本地计数器汇总
        MergeCountersIntoStats(state, counters);
        totalCommittedCount += counters.MergeCount;
    }

    state.Stats.ParallelMergeCommitCount += totalCommittedCount;
}
} // 匿名命名空间

void RefineWithSplitQueue(DataOrientedRoamState& state)
{
    // split pass 用优先队列控制高误差 leaf 的处理顺序
    // 高 screen error leaf 会先获得本帧 split 机会
    state.Stats.TopologyChunkCount = static_cast<std::size_t>(
        DataOrientedRoamTopologyChunkGridSize * DataOrientedRoamTopologyChunkGridSize);
    struct CandidateCompare
    {
        bool operator()(const DataOrientedRoamSplitCandidate& left, const DataOrientedRoamSplitCandidate& right) const
        {
            if (left.Score == right.Score)
            {
                // sequence 保证同分候选的处理顺序稳定
                return left.Sequence > right.Sequence;
            }

            return left.Score < right.Score;
        }
    };

    std::priority_queue<DataOrientedRoamSplitCandidate, std::vector<DataOrientedRoamSplitCandidate>, CandidateCompare> candidates;
    std::uint64_t sequence = 0;

    const auto enqueueCandidateWithScore =
        [&state, &candidates, &sequence](DataOrientedRoamNodeIndex node, float score) {
        if (!state.IsValidNode(node) || !state.IsLeaf(node) || state.Nodes[node].Depth >= state.Settings.MaxDepth)
        {
            // 只有未达到 maxDepth 的 active leaf 进入候选队列
            return;
        }

        // 入队前先过滤一次，减少低价值候选
        if (!ShouldSplitWithScore(state, state.Nodes[node], score))
        {
            return;
        }

        candidates.push(DataOrientedRoamSplitCandidate{score, sequence++, node});
        state.Stats.CandidatePeakCount = std::max(state.Stats.CandidatePeakCount, candidates.size());
    };

    const auto enqueueCandidate = [&state, &enqueueCandidateWithScore](DataOrientedRoamNodeIndex node) {
        const float score = EvaluateScreenErrorForNode(state, node);
        enqueueCandidateWithScore(node, score);
    };

    std::vector<DataOrientedRoamSplitCandidate> initialCandidates;
    CollectSplitCandidates(state, initialCandidates);
    // 并发 batch 只消费 initial snapshot 中的安全子集
    std::vector<std::vector<DataOrientedRoamSplitCandidate>> interiorChunks =
        BuildInteriorSplitChunks(state, initialCandidates);
    const std::vector<CommittedSplit> committedSplits =
        CommitInteriorSplitChunks(state, interiorChunks);

    for (const DataOrientedRoamSplitCandidate& candidate : initialCandidates)
    {
        // 并发 batch 可能已经把 leaf 变成 internal node
        if (state.IsValidNode(candidate.Node) && state.IsLeaf(candidate.Node))
        {
            // 已由并发 batch 提交的 candidate 不再进入串行队列
            candidates.push(candidate);
        }
    }
    sequence = initialCandidates.size();

    for (const CommittedSplit& committedSplit : committedSplits)
    {
        // committed node 仍可能被后续串行约束改变，先做防御检查
        if (!state.IsValidNode(committedSplit.Node))
        {
            continue;
        }

        // 并发 split 产生的新 child 仍进入串行 priority queue 继续细分
        enqueueCandidate(state.Nodes[committedSplit.Node].LeftChild);
        enqueueCandidate(state.Nodes[committedSplit.Node].RightChild);

        // 对侧 diamond child 可能因为本次 split 获得新的细分机会
        if (state.IsValidNode(committedSplit.BaseNeighborBeforeSplit) &&
            !state.IsLeaf(committedSplit.BaseNeighborBeforeSplit))
        {
            enqueueCandidate(state.Nodes[committedSplit.BaseNeighborBeforeSplit].LeftChild);
            enqueueCandidate(state.Nodes[committedSplit.BaseNeighborBeforeSplit].RightChild);
        }
    }

    state.Stats.CandidatePeakCount = std::max(state.Stats.CandidatePeakCount, candidates.size());

    while (!candidates.empty())
    {
        const DataOrientedRoamSplitCandidate candidate = candidates.top();
        candidates.pop();

        // 串行 queue 是并发 batch 之后的最终一致性收尾
        const DataOrientedRoamNodeIndex node = candidate.Node;
        if (!state.IsValidNode(node) || !state.IsLeaf(node))
        {
            // forced split 可能已经拆掉这个候选
            continue;
        }

        const float score = EvaluateScreenErrorForNode(state, node);
        // 弹出时缓存最新分数，便于调试候选过期情况
        // forced split 可能让候选过期
        if (!ShouldSplitWithScore(state, state.Nodes[node], score))
        {
            continue;
        }

        const DataOrientedRoamNodeIndex baseNeighborBeforeSplit = state.Nodes[node].BaseNeighbor;
        if (!SplitNode(state, node, DataOrientedRoamSplitReason::Requested, InvalidDataOrientedRoamNodeIndex))
        {
            // 失败通常来自 maxDepth 或 forced split 传播
            ++state.Stats.RejectedSplitCount;
            continue;
        }

        // split 产生的新 child 不在批量快照内  这里即时评分
        enqueueCandidate(state.Nodes[node].LeftChild);
        enqueueCandidate(state.Nodes[node].RightChild);

        if (state.IsValidNode(baseNeighborBeforeSplit) && !state.IsLeaf(baseNeighborBeforeSplit))
        {
            // base neighbor 可能被约束传播提前 split
            enqueueCandidate(state.Nodes[baseNeighborBeforeSplit].LeftChild);
            enqueueCandidate(state.Nodes[baseNeighborBeforeSplit].RightChild);
        }
    }
}

void MergeWithDiamondQueue(DataOrientedRoamState& state)
{
    // merge pass 先回收低 error 的远处细节
    // 后续 split pass 再补回当前视点需要的细节
    state.Stats.TopologyChunkCount = static_cast<std::size_t>(
        DataOrientedRoamTopologyChunkGridSize * DataOrientedRoamTopologyChunkGridSize);
    std::vector<DataOrientedRoamMergeCandidate> candidates;
    CollectMergeCandidates(state, candidates);

    // merge 仍按低 error 优先，但 interior chunk 可先并发提交
    std::sort(
        candidates.begin(),
        candidates.end(),
        [](const DataOrientedRoamMergeCandidate& left, const DataOrientedRoamMergeCandidate& right) {
            // 低误差优先回收  尽量先减少远处细节
            return left.Score < right.Score;
        });

    std::vector<std::vector<DataOrientedRoamMergeCandidate>> interiorChunks =
        BuildInteriorMergeChunks(state, candidates);
    CommitInteriorMergeChunks(state, interiorChunks);

    for (const DataOrientedRoamMergeCandidate& candidate : candidates)
    {
        // 并发 merge 已处理的 candidate 会在这里自然失效
        if (!CanMergeNode(state, candidate.Node))
        {
            // 前面的 merge 可能改变后面候选的 active 状态
            continue;
        }

        if (!MergeNodeOrDiamond(state, candidate.Node))
        {
            // 串行回退仍记录无法提交的边界候选
            ++state.Stats.RejectedMergeCount;
        }
    }
}
} // 命名空间 ParallelRoam::Algorithms::DataOrientedRoam
