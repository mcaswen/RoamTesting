#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace ParallelRoam::Algorithms::GpuRoam
{
/// <summary>
/// 活动叶压缩 pass 的 OpenGL 资源绑定和扫描范围
/// </summary>
struct GpuRoamActiveLeafCompactionPassInput
{
    std::uint32_t ProgramId{0};
    std::uint32_t NodeBufferId{0};
    std::uint32_t ActiveLeafBufferId{0};
    std::uint32_t CounterBufferId{0};
    std::size_t NodeCount{0};
};

[[nodiscard]] bool EnsureGpuRoamActiveLeafCompactionProgram(
    std::uint32_t& programId,
    std::string* errorMessage);

void RunGpuRoamActiveLeafCompactionPass(const GpuRoamActiveLeafCompactionPassInput& input);
} // namespace ParallelRoam::Algorithms::GpuRoam
