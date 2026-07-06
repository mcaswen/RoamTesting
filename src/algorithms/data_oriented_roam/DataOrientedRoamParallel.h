#pragma once

#include "algorithms/data_oriented_roam/DataOrientedRoamState.h"
#include "algorithms/data_oriented_roam/DataOrientedRoamThreadPool.h"

#include <cstddef>
#include <functional>

namespace ParallelRoam::Algorithms::DataOrientedRoam
{
inline void RunDataOrientedRoamWorkers(
    DataOrientedRoamState& state,
    std::size_t workerCount,
    const std::function<void(std::size_t workerIndex)>& task)
{
    if (workerCount == 0U)
    {
        return;
    }

    if (workerCount == 1U)
    {
        // 单 worker 直接在调用线程执行，避免小任务进入队列
        task(0U);
        return;
    }

    if (state.ThreadPool != nullptr)
    {
        // 线程池由 builder 持有，pass 只提交按 workerIndex 切好的任务
        state.ThreadPool->ParallelFor(workerCount, task);
        return;
    }

    for (std::size_t workerIndex = 0U; workerIndex < workerCount; ++workerIndex)
    {
        // 测试或迁移期间没有线程池时，保持确定性的串行回退
        task(workerIndex);
    }
}
} // namespace ParallelRoam::Algorithms::DataOrientedRoam
