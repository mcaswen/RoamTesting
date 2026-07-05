#pragma once

#include <algorithm>
#include <chrono>
#include <ctime>

namespace ParallelRoam::Algorithms
{
/// <summary>
/// 捕获一次 LOD build 前后的墙钟时间和进程 CPU 时间
/// </summary>
struct TerrainLodCpuSample
{
    // WallTime 用于计算本次 build 的真实等待时间
    std::chrono::steady_clock::time_point WallTime;
    // ProcessCpuTime 会累加同进程内多个 worker 的 CPU 时间
    std::clock_t ProcessCpuTime{0};
};

[[nodiscard]] inline TerrainLodCpuSample CaptureTerrainLodCpuSample()
{
    return TerrainLodCpuSample{
        std::chrono::steady_clock::now(),
        std::clock(),
    };
}

[[nodiscard]] inline float ComputeCpuUtilizationPercent(
    const TerrainLodCpuSample& start,
    const TerrainLodCpuSample& end)
{
    const auto wallDuration = std::chrono::duration<double, std::milli>(end.WallTime - start.WallTime);
    if (wallDuration.count() <= 0.0 || start.ProcessCpuTime == static_cast<std::clock_t>(-1) ||
        end.ProcessCpuTime == static_cast<std::clock_t>(-1))
    {
        return 0.0F;
    }

    // 100% 表示一个逻辑核心满载  多线程 build 可以超过 100%
    const double cpuMilliseconds =
        static_cast<double>(end.ProcessCpuTime - start.ProcessCpuTime) * 1000.0 / CLOCKS_PER_SEC;
    return static_cast<float>(std::max(0.0, cpuMilliseconds / wallDuration.count() * 100.0));
}
} // 命名空间 ParallelRoam::Algorithms
