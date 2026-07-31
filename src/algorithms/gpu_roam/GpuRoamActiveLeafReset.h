#pragma once

#include <cstdint>
#include <string>

namespace ParallelRoam::Algorithms::GpuRoam
{
// Compiles and caches the one-invocation reset shader used between split and
// final compaction. Keeping this as a named pass makes backend timings comparable.
[[nodiscard]] bool EnsureGpuRoamActiveLeafResetProgram(
    std::uint32_t& programId,
    std::string* errorMessage);

// Resets ActiveLeafCount while preserving allocation, budget and split counters.
// The function records commands only; completion remains asynchronous.
void RunGpuRoamActiveLeafResetPass(std::uint32_t programId, std::uint32_t counterBufferId);
} // namespace ParallelRoam::Algorithms::GpuRoam
