#pragma once

#include <cstdint>
#include <string>

namespace ParallelRoam::Algorithms::GpuRoam
{
// 编译并缓存 split 和最终 compaction 之间使用的单 invocation reset shader
// 将它作为独立 pass 保留，便于后端计时口径一致
[[nodiscard]] bool EnsureGpuRoamActiveLeafResetProgram(
    std::uint32_t& programId,
    std::string* errorMessage);

// 在保留 allocation、budget 和 split counter 的同时重置 ActiveLeafCount
// 该函数只记录命令，完成过程仍然异步执行
void RunGpuRoamActiveLeafResetPass(std::uint32_t programId, std::uint32_t counterBufferId);
} // namespace ParallelRoam::Algorithms::GpuRoam
