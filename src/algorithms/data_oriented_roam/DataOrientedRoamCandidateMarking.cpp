#include "algorithms/data_oriented_roam/DataOrientedRoamParallel.h"
#include "algorithms/data_oriented_roam/DataOrientedRoamState.h"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <thread>
#include <utility>
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

struct FusedSplitScanOutput
{
    // 候选只写 worker-local vector，扫描阶段不需要互斥锁。
    std::vector<DataOrientedRoamSplitCandidate> Candidates;
};
} // 匿名命名空间

bool CanMergeNode(const DataOrientedRoamState& state, DataOrientedRoamNodeIndex node)
{
    return CanMergeNode(state, node, state.Settings.MergeThreshold);
}

bool CanMergeNode(
    const DataOrientedRoamState& state,
    DataOrientedRoamNodeIndex node,
    float maximumScore)
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

    if (ComputeScreenErrorScore(state, candidate) > maximumScore)
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

    return ComputeScreenErrorScore(state, state.Nodes[baseNeighbor]) <= maximumScore;
}

void CollectSplitCandidates(DataOrientedRoamState& state, std::vector<DataOrientedRoamSplitCandidate>& candidates)
{
    candidates.clear();
    const auto start = std::chrono::steady_clock::now();
    const std::size_t leafCount = state.ActiveLeafNodes.size();
    const std::size_t workerCount = ResolveTopologyWorkerCount(state, leafCount);
    // 一个物理 pass 同时承担 active leaf 判定、SSE/视锥评估和 split 标记。
    state.Stats.CollectWorkerCount = std::max(state.Stats.CollectWorkerCount, workerCount);
    state.Stats.ErrorEvaluationWorkerCount = workerCount;
    state.Stats.CandidateMarkWorkerCount = std::max(state.Stats.CandidateMarkWorkerCount, workerCount);
    // 旧字段继续输出，零值明确表示职责已融合而不是没有执行。
    state.Stats.ActiveLeafCollectMilliseconds = 0.0F;
    state.Stats.ErrorEvaluationSingleThreadMilliseconds = 0.0F;
    state.Stats.ErrorEvaluationParallelMilliseconds = 0.0F;

    if (leafCount == 0U)
    {
        // 空池没有 active leaf，完整预算可供后续防御路径使用。
        state.Stats.ErrorEvaluationCount = 0U;
        state.RemainingSplitBudget.store(state.Settings.TriangleBudget, std::memory_order_relaxed);
        return;
    }

    const auto scanRange =
        [&state](std::size_t begin, std::size_t end, FusedSplitScanOutput& output) {
        // 每个 worker 写互不重叠的 ScreenErrors 槽位和自己的候选 buffer。
        // ActiveLeafNodes 在 topology commit 前只读稳定，因此无需重新检查 parent 链。
        for (std::size_t index = begin; index < end; ++index)
        {
            const DataOrientedRoamNodeIndex node = state.ActiveLeafNodes[index];
            if (!state.IsValidNode(node) || !state.IsLeaf(node))
            {
                // validator 会捕获索引不变量异常；扫描本身保持防御性跳过。
                continue;
            }

            // score 只计算一次，同时供当前候选和后续诊断读取。
            const DataOrientedRoamNodePool& nodes = state.Nodes;
            const float score = ComputeScreenErrorScore(state, nodes[node]);
            state.Nodes.ScreenErrors[node] = score;
            if (nodes.Depths[node] < state.Settings.MaxDepth &&
                ShouldSplitWithScore(state, nodes[node], score))
            {
                // max depth 与 hysteresis 在生成候选前过滤，降低 priority queue 压力。
                output.Candidates.push_back(DataOrientedRoamSplitCandidate{score, 0U, node});
            }
        }
    };

    if (workerCount <= 1U)
    {
        // 串行路径复用相同扫描函数，保持 active 判定与并行路径一致。
        FusedSplitScanOutput output;
        scanRange(0U, leafCount, output);
        candidates = std::move(output.Candidates);
    }
    else
    {
        const std::size_t chunkSize = (leafCount + workerCount - 1U) / workerCount;
        // localOutputs 的下标就是稳定的 active-leaf range 顺序。
        std::vector<FusedSplitScanOutput> localOutputs(workerCount);
        RunDataOrientedRoamWorkers(state, workerCount, [&](std::size_t workerIndex) {
            const std::size_t begin = workerIndex * chunkSize;
            const std::size_t end = std::min(begin + chunkSize, leafCount);
            if (begin >= end)
            {
                return;
            }

            scanRange(begin, end, localOutputs[workerIndex]);
        });

        std::size_t totalCandidateCount = 0U;
        for (const FusedSplitScanOutput& output : localOutputs)
        {
            // 先汇总总量，主候选 vector 只扩容一次。
            totalCandidateCount += output.Candidates.size();
        }

        candidates.reserve(totalCandidateCount);
        for (FusedSplitScanOutput& output : localOutputs)
        {
            // 按 worker range 顺序合并，使相同 score 的 sequence 跨帧确定。
            candidates.insert(candidates.end(), output.Candidates.begin(), output.Candidates.end());
        }
    }

    for (std::size_t index = 0U; index < candidates.size(); ++index)
    {
        // sequence 在合并后统一分配  避免 worker 局部编号冲突
        candidates[index].Sequence = index;
    }

    state.Stats.ErrorEvaluationCount = leafCount;
    state.Stats.SplitCandidateCount = candidates.size();
    // 每次成功 split 净增一个 active leaf，因此当前 leaf 数可直接换算剩余 token。
    // 使用索引原始大小能在索引损坏时保持预算保守，validator 会报告具体不变量错误。
    const std::size_t remainingBudget = state.Settings.TriangleBudget > leafCount
        ? state.Settings.TriangleBudget - leafCount
        : 0U;
    state.RemainingSplitBudget.store(remainingBudget, std::memory_order_relaxed);
    // 融合区间统一归入 split candidate mark；独立 collect/error 字段保持为零。
    // 这样互斥阶段之和仍能还原 SplitMilliseconds，不会重复计算同一循环。
    state.Stats.SplitCandidateMarkMilliseconds = ElapsedMilliseconds(start, std::chrono::steady_clock::now());
}

void CollectMergeCandidates(DataOrientedRoamState& state, std::vector<DataOrientedRoamMergeCandidate>& candidates)
{
    CollectMergeCandidates(state, candidates, state.Settings.MergeThreshold);
}

void CollectMergeCandidates(
    DataOrientedRoamState& state,
    std::vector<DataOrientedRoamMergeCandidate>& candidates,
    float maximumScore)
{
    candidates.clear();
    const auto start = std::chrono::steady_clock::now();
    const std::size_t nodeCount = state.ActiveInternalNodes.size();
    const std::size_t workerCount = ResolveTopologyWorkerCount(state, nodeCount);
    // merge 标记只扫描当前 active internal 索引，不再触碰历史 inactive node pool
    state.Stats.CandidateMarkWorkerCount = std::max(state.Stats.CandidateMarkWorkerCount, workerCount);

    if (nodeCount == 0U)
    {
        // merge 扫描没有节点时保持空候选
        return;
    }

    const auto markRange =
        [&state, maximumScore](
            std::size_t begin,
            std::size_t end,
            std::vector<DataOrientedRoamMergeCandidate>& outCandidates) {
        // CanMergeNode 只读拓扑  真正 merge 仍在串行提交流程
        for (std::size_t index = begin; index < end; ++index)
        {
            const DataOrientedRoamNodeIndex node = state.ActiveInternalNodes[index];
            if (!state.IsValidNode(node) ||
                state.IsLeaf(node) ||
                !CanMergeNode(state, node, maximumScore))
            {
                // 索引不变量异常或当前不可回收的节点都不会进入 merge 队列
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

    state.Stats.MergeCandidateCount += candidates.size();
    state.Stats.MergeCandidateMarkMilliseconds += ElapsedMilliseconds(start, std::chrono::steady_clock::now());
}
} // 命名空间 ParallelRoam::Algorithms::DataOrientedRoam
