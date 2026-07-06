#include "algorithms/classic_roam/ClassicRoamMeshBuilder.h"

#include <algorithm>
#include <cstdint>
#include <queue>
#include <vector>

namespace ParallelRoam::Algorithms::ClassicRoam
{
namespace
{
struct SplitEdge
{
    // Start 和 End 是本次 split 的边，Apex 是该边对面的顶点
    glm::vec2 Start{0.0F};
    glm::vec2 End{0.0F};
    glm::vec2 Apex{0.0F};
};

SplitEdge ChooseBaseEdge(const TriangleDomain& domain)
{
    // Classic ROAM 固定沿 base edge split，A/B 是 base 两端
    return SplitEdge{domain.A, domain.B, domain.C};
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
} // 匿名命名空间

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
    // priority queue 先处理最高 screen error 的 leaf
    // 避免递归遍历顺序影响最终细分分布
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
        // 入队前先过滤低价值候选，减少后续 priority queue 压力
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

        ClassicRoamNode* baseNeighborBeforeSplit = node->BaseNeighbor;
        if (!SplitNode(node, SplitReason::Requested, nullptr))
        {
            // forced split 传播失败时，该候选不能继续展开
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

        // CanMergeNode 已包含 diamond 形状和 error 阈值判断
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
        // 已 split 的节点由 child 承担更细层级决策
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
            ++_stats.ConstraintPassCount;
            // 经典 ROAM 要求 baseNeighbor 先回到互为 base 的 diamond 关系
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
    // forced 标记只用于 debug color，不改变拓扑语义
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
        // terrain 边界没有邻居需要修复
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
        // 历史数据损坏时拒绝 merge，交给 validator 计数
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
        // 外层队列可能持有已过期的 merge candidate
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
} // 命名空间 ParallelRoam::Algorithms::ClassicRoam
