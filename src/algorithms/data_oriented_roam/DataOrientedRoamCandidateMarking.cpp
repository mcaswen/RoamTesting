#include "algorithms/data_oriented_roam/DataOrientedRoamParallel.h"
#include "algorithms/data_oriented_roam/DataOrientedRoamState.h"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <thread>
#include <vector>

namespace ParallelRoam::Algorithms::DataOrientedRoam
{
namespace
{
constexpr std::size_t MaxAutoWorkerCount = 8;
// 小任务保持串行  避免并行调度成本盖过扫描收益
constexpr std::size_t MinParallelWorkItemCount = 256;

std::size_t ResolveTopologyWorkerCount(const DataOrientedRoamState& state, std::size_t workItemCount)
{
    if (workItemCount == 0U)
    {
        return 0U;
    }

    if (state.Settings.ErrorEvaluationWorkerCount == 1U || workItemCount < MinParallelWorkItemCount)
    {
        // worker 设置沿用误差评估参数  避免扩大 UI 参数面
        return 1U;
    }

    std::size_t requestedWorkerCount = state.Settings.ErrorEvaluationWorkerCount;
    if (requestedWorkerCount == 0U)
    {
        const unsigned int hardwareWorkerCount = std::thread::hardware_concurrency();
        requestedWorkerCount = hardwareWorkerCount == 0U ? 1U : static_cast<std::size_t>(hardwareWorkerCount);
        requestedWorkerCount = std::min(requestedWorkerCount, MaxAutoWorkerCount);
    }

    return std::clamp(requestedWorkerCount, std::size_t{1}, workItemCount);
}

bool IsActiveTopologyNode(const DataOrientedRoamState& state, DataOrientedRoamNodeIndex node)
{
    if (!state.IsValidNode(node))
    {
        return false;
    }

    DataOrientedRoamNodeIndex cursor = node;
    // 只要祖先有一个已经 merge  当前 node 就是 inactive 历史节点
    while (state.IsValidNode(state.Nodes[cursor].Parent))
    {
        const DataOrientedRoamNodeIndex parent = state.Nodes[cursor].Parent;
        if (!state.Nodes[parent].IsSplit)
        {
            // parent 已 merge 时，旧 child 仍在 node pool 但不属于 active topology
            return false;
        }

        cursor = parent;
    }

    // parent 链最终必须回到两个 root 之一
    return cursor == state.RootA || cursor == state.RootB;
}

void CollectActiveLeafNodes(DataOrientedRoamState& state, std::vector<DataOrientedRoamNodeIndex>& leafNodes)
{
    leafNodes.clear();
    const auto start = std::chrono::steady_clock::now();
    const std::size_t nodeCount = state.Nodes.size();
    const std::size_t workerCount = ResolveTopologyWorkerCount(state, nodeCount);
    // collect worker 数用于判断 active leaf 扫描是否进入并行路径
    state.Stats.CollectWorkerCount = std::max(state.Stats.CollectWorkerCount, workerCount);

    if (nodeCount == 0U)
    {
        // 空 node pool 只会出现在输入无效或 reset 前后
        return;
    }

    const auto collectRange =
        [&state](std::size_t begin, std::size_t end, std::vector<DataOrientedRoamNodeIndex>& outLeaves) {
        // node index 范围固定  每个 worker 只写自己的 local buffer
        for (std::size_t index = begin; index < end; ++index)
        {
            const auto node = static_cast<DataOrientedRoamNodeIndex>(index);
            if (state.IsLeaf(node) && IsActiveTopologyNode(state, node))
            {
                outLeaves.push_back(node);
            }
        }
    };

    if (workerCount <= 1U)
    {
        // 串行路径复用同一个 range 函数  保持筛选口径一致
        collectRange(0U, nodeCount, leafNodes);
        state.Stats.ActiveLeafCollectMilliseconds = ElapsedMilliseconds(start, std::chrono::steady_clock::now());
        return;
    }

    const std::size_t chunkSize = (nodeCount + workerCount - 1U) / workerCount;
    std::vector<std::vector<DataOrientedRoamNodeIndex>> localLeaves(workerCount);
    // localLeaves 避免多个 worker 同时 push 同一个 vector
    RunDataOrientedRoamWorkers(state, workerCount, [&](std::size_t workerIndex) {
        const std::size_t begin = workerIndex * chunkSize;
        const std::size_t end = std::min(begin + chunkSize, nodeCount);
        if (begin >= end)
        {
            return;
        }

        collectRange(begin, end, localLeaves[workerIndex]);
    });

    std::size_t totalLeafCount = 0U;
    for (const std::vector<DataOrientedRoamNodeIndex>& localBuffer : localLeaves)
    {
        // 先统计总数  再一次 reserve 最终输出
        totalLeafCount += localBuffer.size();
    }

    leafNodes.reserve(totalLeafCount);
    for (std::vector<DataOrientedRoamNodeIndex>& localBuffer : localLeaves)
    {
        // 按 chunk 顺序合并  保持候选顺序稳定
        leafNodes.insert(leafNodes.end(), localBuffer.begin(), localBuffer.end());
    }

    state.Stats.ActiveLeafCollectMilliseconds = ElapsedMilliseconds(start, std::chrono::steady_clock::now());
}
} // 匿名命名空间

bool CanMergeNode(const DataOrientedRoamState& state, DataOrientedRoamNodeIndex node)
{
    if (!state.IsValidNode(node) || state.IsLeaf(node))
    {
        // merge candidate 必须是 active internal node
        return false;
    }

    const DataOrientedRoamNodeConstRef candidate = state.Nodes[node];
    if (!state.IsValidNode(candidate.LeftChild) || !state.IsValidNode(candidate.RightChild))
    {
        return false;
    }

    if (!state.IsLeaf(candidate.LeftChild) || !state.IsLeaf(candidate.RightChild))
    {
        // 子树更深时必须先从更深层回收
        return false;
    }

    if (ComputeScreenErrorScore(state, candidate) > state.Settings.MergeThreshold)
    {
        // parent 自身误差仍高时回收会造成可见 LOD 退化
        return false;
    }

    const DataOrientedRoamNodeIndex baseNeighbor = candidate.BaseNeighbor;
    if (!state.IsValidNode(baseNeighbor) || state.IsLeaf(baseNeighbor))
    {
        return true;
    }

    if (state.Nodes[baseNeighbor].BaseNeighbor != node)
    {
        // 非互指 diamond 不能单侧 merge，否则会制造 T-junction
        return false;
    }

    if (!state.IsValidNode(state.Nodes[baseNeighbor].LeftChild) ||
        !state.IsValidNode(state.Nodes[baseNeighbor].RightChild))
    {
        return false;
    }

    if (!state.IsLeaf(state.Nodes[baseNeighbor].LeftChild) || !state.IsLeaf(state.Nodes[baseNeighbor].RightChild))
    {
        return false;
    }

    return ComputeScreenErrorScore(state, state.Nodes[baseNeighbor]) <= state.Settings.MergeThreshold;
}

void CollectSplitCandidates(DataOrientedRoamState& state, std::vector<DataOrientedRoamSplitCandidate>& candidates)
{
    candidates.clear();
    std::vector<DataOrientedRoamNodeIndex> activeLeaves;
    // 批量评分只覆盖进入 split pass 前已有的 active leaf
    CollectActiveLeafNodes(state, activeLeaves);
    // 评估完成前不修改拓扑  leaf index 快照保持有效
    EvaluateScreenErrors(state, activeLeaves);

    const auto start = std::chrono::steady_clock::now();
    const std::size_t workerCount = ResolveTopologyWorkerCount(state, activeLeaves.size());
    // split 和 merge 标记共享同一个 worker 统计
    state.Stats.CandidateMarkWorkerCount = std::max(state.Stats.CandidateMarkWorkerCount, workerCount);

    if (activeLeaves.empty())
    {
        // 没有 active leaf 时无需启动候选标记
        return;
    }

    const auto markRange =
        [&state, &activeLeaves](std::size_t begin, std::size_t end, std::vector<DataOrientedRoamSplitCandidate>& outCandidates) {
        // 这里只读缓存分数  不重复采样 HeightMap
        for (std::size_t index = begin; index < end; ++index)
        {
            const DataOrientedRoamNodeIndex node = activeLeaves[index];
            if (!state.IsValidNode(node) || !state.IsLeaf(node) || state.Nodes[node].Depth >= state.Settings.MaxDepth)
            {
                // 并行评估后拓扑仍可能因其他约束变化而使候选失效
                continue;
            }

            const float score = state.Nodes[node].ScreenError;
            // hysteresis 仍在 ShouldSplitWithScore 内统一处理
            if (ShouldSplitWithScore(state, state.Nodes[node], score))
            {
                outCandidates.push_back(DataOrientedRoamSplitCandidate{score, 0U, node});
            }
        }
    };

    if (workerCount <= 1U)
    {
        // 小批量 leaf 不走并行调度  减少帧间抖动
        markRange(0U, activeLeaves.size(), candidates);
    }
    else
    {
        const std::size_t chunkSize = (activeLeaves.size() + workerCount - 1U) / workerCount;
        std::vector<std::vector<DataOrientedRoamSplitCandidate>> localCandidates(workerCount);
        // 每个 worker 的候选列表独立增长  不需要锁
        RunDataOrientedRoamWorkers(state, workerCount, [&](std::size_t workerIndex) {
            const std::size_t begin = workerIndex * chunkSize;
            const std::size_t end = std::min(begin + chunkSize, activeLeaves.size());
            if (begin >= end)
            {
                return;
            }

            markRange(begin, end, localCandidates[workerIndex]);
        });

        std::size_t totalCandidateCount = 0U;
        for (const std::vector<DataOrientedRoamSplitCandidate>& localBuffer : localCandidates)
        {
            // 合并前计算总量  避免反复扩容
            totalCandidateCount += localBuffer.size();
        }

        candidates.reserve(totalCandidateCount);
        for (std::vector<DataOrientedRoamSplitCandidate>& localBuffer : localCandidates)
        {
            // chunk 顺序即稳定 sequence 的基础
            candidates.insert(candidates.end(), localBuffer.begin(), localBuffer.end());
        }
    }

    for (std::size_t index = 0U; index < candidates.size(); ++index)
    {
        // sequence 在合并后统一分配  避免 worker 局部编号冲突
        candidates[index].Sequence = index;
    }

    state.Stats.SplitCandidateCount = candidates.size();
    state.Stats.SplitCandidateMarkMilliseconds = ElapsedMilliseconds(start, std::chrono::steady_clock::now());
}

void CollectMergeCandidates(DataOrientedRoamState& state, std::vector<DataOrientedRoamMergeCandidate>& candidates)
{
    candidates.clear();
    const auto start = std::chrono::steady_clock::now();
    const std::size_t nodeCount = state.Nodes.size();
    const std::size_t workerCount = ResolveTopologyWorkerCount(state, nodeCount);
    // merge 标记扫描整个 node pool  但只接受 active internal node
    state.Stats.CandidateMarkWorkerCount = std::max(state.Stats.CandidateMarkWorkerCount, workerCount);

    if (nodeCount == 0U)
    {
        // merge 扫描没有节点时保持空候选
        return;
    }

    const auto markRange =
        [&state](std::size_t begin, std::size_t end, std::vector<DataOrientedRoamMergeCandidate>& outCandidates) {
        // CanMergeNode 只读拓扑  真正 merge 仍在串行提交流程
        for (std::size_t index = begin; index < end; ++index)
        {
            const auto node = static_cast<DataOrientedRoamNodeIndex>(index);
            if (state.IsLeaf(node) || !IsActiveTopologyNode(state, node) || !CanMergeNode(state, node))
            {
                // inactive 历史节点和 leaf 都不会进入 merge 队列
                continue;
            }

            const float score = ComputeScreenErrorScore(state, state.Nodes[node]);
            // merge 候选也刷新同一份 score cache
            state.Nodes[node].ScreenError = score;
            outCandidates.push_back(DataOrientedRoamMergeCandidate{score, node});
        }
    };

    if (workerCount <= 1U)
    {
        // 串行路径便于小拓扑避免额外 worker 成本
        markRange(0U, nodeCount, candidates);
    }
    else
    {
        const std::size_t chunkSize = (nodeCount + workerCount - 1U) / workerCount;
        std::vector<std::vector<DataOrientedRoamMergeCandidate>> localCandidates(workerCount);
        // merge candidate 同样使用 thread-local buffer
        RunDataOrientedRoamWorkers(state, workerCount, [&](std::size_t workerIndex) {
            const std::size_t begin = workerIndex * chunkSize;
            const std::size_t end = std::min(begin + chunkSize, nodeCount);
            if (begin >= end)
            {
                return;
            }

            markRange(begin, end, localCandidates[workerIndex]);
        });

        std::size_t totalCandidateCount = 0U;
        for (const std::vector<DataOrientedRoamMergeCandidate>& localBuffer : localCandidates)
        {
            // 汇总候选数量后再合并输出
            totalCandidateCount += localBuffer.size();
        }

        candidates.reserve(totalCandidateCount);
        for (std::vector<DataOrientedRoamMergeCandidate>& localBuffer : localCandidates)
        {
            // 排序前只要求确定性合并  不要求预先有序
            candidates.insert(candidates.end(), localBuffer.begin(), localBuffer.end());
        }
    }

    state.Stats.MergeCandidateCount = candidates.size();
    state.Stats.MergeCandidateMarkMilliseconds = ElapsedMilliseconds(start, std::chrono::steady_clock::now());
}
} // 命名空间 ParallelRoam::Algorithms::DataOrientedRoam
