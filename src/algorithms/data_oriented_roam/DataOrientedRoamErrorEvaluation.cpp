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
constexpr std::size_t MinParallelLeafCount = 256;

std::size_t ResolveWorkerCount(const DataOrientedRoamState& state, std::size_t workItemCount)
{
    if (workItemCount == 0U)
    {
        return 0U;
    }

    if (state.Settings.ErrorEvaluationWorkerCount == 1U || workItemCount < MinParallelLeafCount)
    {
        // 小批量 leaf 的 thread 创建成本通常高于并行收益
        return 1U;
    }

    std::size_t requestedWorkerCount = state.Settings.ErrorEvaluationWorkerCount;
    if (requestedWorkerCount == 0U)
    {
        // 零值代表交给运行时按硬件能力选择
        const unsigned int hardwareWorkerCount = std::thread::hardware_concurrency();
        requestedWorkerCount = hardwareWorkerCount == 0U ? 1U : static_cast<std::size_t>(hardwareWorkerCount);
        // 自动模式保守封顶  避免小场景启动过多 worker
        requestedWorkerCount = std::min(requestedWorkerCount, MaxAutoWorkerCount);
    }

    // worker 数不能超过待评估 leaf 数
    return std::clamp(requestedWorkerCount, std::size_t{1}, workItemCount);
}

void EvaluateRange(
    DataOrientedRoamState& state,
    const std::vector<DataOrientedRoamNodeIndex>& leafNodes,
    std::size_t begin,
    std::size_t end)
{
    for (std::size_t index = begin; index < end; ++index)
    {
        const DataOrientedRoamNodeIndex node = leafNodes[index];
        if (!state.IsValidNode(node))
        {
            continue;
        }

        // const view 避免评估 pass 误写拓扑字段
        const DataOrientedRoamNodePool& nodes = state.Nodes;
        const float score = ComputeScreenErrorScore(state, nodes[node]);
        // 每个 leaf 只写自己的 SoA 槽位  不需要共享锁
        state.Nodes.ScreenErrors[node] = score;
    }
}
} // 匿名命名空间

float EvaluateScreenErrorForNode(DataOrientedRoamState& state, DataOrientedRoamNodeIndex node)
{
    if (!state.IsValidNode(node))
    {
        return 0.0F;
    }

    // split 过程中新增 child 走即时评分
    // 保持队列候选和批量评估使用同一公式
    const DataOrientedRoamNodePool& nodes = state.Nodes;
    const float score = ComputeScreenErrorScore(state, nodes[node]);
    state.Nodes.ScreenErrors[node] = score;
    return score;
}

void EvaluateScreenErrors(DataOrientedRoamState& state, const std::vector<DataOrientedRoamNodeIndex>& leafNodes)
{
    // 统计只覆盖本次批量 active leaf 快照
    state.Stats.ErrorEvaluationCount = leafNodes.size();
    state.Stats.ErrorEvaluationWorkerCount = ResolveWorkerCount(state, leafNodes.size());
    state.Stats.ErrorEvaluationSingleThreadMilliseconds = 0.0F;
    state.Stats.ErrorEvaluationParallelMilliseconds = 0.0F;

    if (leafNodes.empty())
    {
        return;
    }

    const auto start = std::chrono::steady_clock::now();
    const std::size_t workerCount = state.Stats.ErrorEvaluationWorkerCount;
    if (workerCount <= 1U)
    {
        // 单线程路径覆盖小批量和显式 worker=1
        EvaluateRange(state, leafNodes, 0U, leafNodes.size());
        state.Stats.ErrorEvaluationSingleThreadMilliseconds = ElapsedMilliseconds(start, std::chrono::steady_clock::now());
        return;
    }

    std::vector<std::thread> workers;
    // 每个 worker 只处理一个连续 chunk
    workers.reserve(workerCount);
    // 连续 leaf index 分段能减少调度元数据
    const std::size_t chunkSize = (leafNodes.size() + workerCount - 1U) / workerCount;
    for (std::size_t workerIndex = 0U; workerIndex < workerCount; ++workerIndex)
    {
        const std::size_t begin = workerIndex * chunkSize;
        const std::size_t end = std::min(begin + chunkSize, leafNodes.size());
        if (begin >= end)
        {
            break;
        }

        // lambda 只捕获稳定快照和 range 边界
        workers.emplace_back([&state, &leafNodes, begin, end]() {
            EvaluateRange(state, leafNodes, begin, end);
        });
    }

    for (std::thread& worker : workers)
    {
        worker.join();
    }

    // join 完成后 split queue 才能读取全部缓存分数
    state.Stats.ErrorEvaluationParallelMilliseconds = ElapsedMilliseconds(start, std::chrono::steady_clock::now());
}
} // 命名空间 ParallelRoam::Algorithms::DataOrientedRoam
