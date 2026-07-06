#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace ParallelRoam::Algorithms::GpuRoam
{
/// <summary>
/// GPU split-only topology pass 的输入资源，所有 buffer 都由 GPU ROAM adapter 持有
/// </summary>
struct GpuRoamSplitOnlyTopologyPassInput
{
    std::uint32_t NodeBufferId{0};
    std::uint32_t SplitCandidateBufferId{0};
    std::uint32_t CounterBufferId{0};
    std::size_t CandidateDispatchCount{0};
    std::size_t NodeCapacity{0};
    int MaxDepth{0};
    std::uint64_t BuildSequence{0};
};

[[nodiscard]] bool RunGpuRoamSplitOnlyTopologyPass(
    std::uint32_t& programId,
    const GpuRoamSplitOnlyTopologyPassInput& input,
    std::string* errorMessage);
} // namespace ParallelRoam::Algorithms::GpuRoam
