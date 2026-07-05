#include "algorithms/data_oriented_roam/DataOrientedRoamState.h"

#include <algorithm>
#include <queue>
#include <vector>

namespace ParallelRoam::Algorithms::DataOrientedRoam
{
namespace
{
struct SplitEdge
{
    // Start 和 End 是 base edge 两端
    glm::vec2 Start{0.0F};
    glm::vec2 End{0.0F};
    // Apex 是 base edge 对侧顶点
    glm::vec2 Apex{0.0F};
};

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
    DataOrientedRoamNodeIndex forcedFrom)
{
    if (!state.IsValidNode(node) || !state.IsLeaf(node))
    {
        // internal node 已经由 child 接管细分决策
        return false;
    }

    if (state.Nodes[node].Depth >= state.Settings.MaxDepth)
    {
        // maxDepth 是硬限制，不进入约束传播
        ++state.Stats.RejectedSplitCount;
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
            ++state.Stats.ConstraintPassCount;
            if (!SplitNode(state, baseNeighbor, DataOrientedRoamSplitReason::ForcedByBaseNeighbor, node))
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
        ++state.Stats.ConstraintPassCount;
        if (!SplitNode(state, baseNeighbor, DataOrientedRoamSplitReason::ForcedByBaseNeighbor, node))
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
    // parent path 进入本帧 split 集合，后续用于 hysteresis
    state.CurrentSplitPaths.insert(parentPathId);
    ++state.Stats.SplitCount;
    if (reason != DataOrientedRoamSplitReason::Requested)
    {
        ++state.Stats.ForcedSplitCount;
    }
    return true;
}

void MergeSingleNode(DataOrientedRoamState& state, DataOrientedRoamNodeIndex node)
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
    ++state.Stats.MergeCount;
}

bool MergeNodeOrDiamond(DataOrientedRoamState& state, DataOrientedRoamNodeIndex node)
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
        MergeSingleNode(state, node);
        MergeSingleNode(state, baseNeighbor);
        state.Nodes[node].BaseNeighbor = baseNeighbor;
        state.Nodes[baseNeighbor].BaseNeighbor = node;
        return true;
    }

    MergeSingleNode(state, node);
    return true;
}
} // 匿名命名空间

void RefineWithSplitQueue(DataOrientedRoamState& state)
{
    // split pass 用优先队列控制预算分配
    // 高 screen error leaf 会先获得本帧 split 机会
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
    for (const DataOrientedRoamSplitCandidate& candidate : initialCandidates)
    {
        candidates.push(candidate);
    }
    sequence = initialCandidates.size();
    state.Stats.CandidatePeakCount = std::max(state.Stats.CandidatePeakCount, candidates.size());

    while (!candidates.empty())
    {
        const DataOrientedRoamSplitCandidate candidate = candidates.top();
        candidates.pop();

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

        if (state.Settings.SplitBudget > 0U && state.Stats.SplitCount >= state.Settings.SplitBudget)
        {
            ++state.Stats.RejectedSplitCount;
            break;
        }

        const DataOrientedRoamNodeIndex baseNeighborBeforeSplit = state.Nodes[node].BaseNeighbor;
        if (!SplitNode(state, node, DataOrientedRoamSplitReason::Requested, InvalidDataOrientedRoamNodeIndex))
        {
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
    std::vector<DataOrientedRoamMergeCandidate> candidates;
    CollectMergeCandidates(state, candidates);

    std::sort(
        candidates.begin(),
        candidates.end(),
        [](const DataOrientedRoamMergeCandidate& left, const DataOrientedRoamMergeCandidate& right) {
            // 低误差优先回收  尽量先减少远处细节
            return left.Score < right.Score;
        });

    for (const DataOrientedRoamMergeCandidate& candidate : candidates)
    {
        if (!CanMergeNode(state, candidate.Node))
        {
            // 前面的 merge 可能改变后面候选的 active 状态
            continue;
        }

        if (!MergeNodeOrDiamond(state, candidate.Node))
        {
            ++state.Stats.RejectedMergeCount;
        }
    }
}
} // 命名空间 ParallelRoam::Algorithms::DataOrientedRoam
