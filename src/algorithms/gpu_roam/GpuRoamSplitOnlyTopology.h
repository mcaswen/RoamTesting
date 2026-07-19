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
    std::uint32_t NodeBufferId{0}; // 原地修改的节点池
    std::uint32_t SplitCandidateBufferId{0}; // candidate pass 生成的节点索引
    std::uint32_t CounterBufferId{0}; // 候选数、提交数和节点尾指针
    std::size_t CandidateDispatchCount{0}; // dispatch 防御上界
    std::size_t NodeCapacity{0}; // 原子分配不得超过的节点数
    int MaxDepth{0}; // 子节点深度硬上限
    std::uint64_t BuildSequence{0}; // 新节点的创建和激活版本
};

[[nodiscard]] bool RunGpuRoamSplitOnlyTopologyPass(
    std::uint32_t& programId,
    const GpuRoamSplitOnlyTopologyPassInput& input,
    std::string* errorMessage);
} // namespace ParallelRoam::Algorithms::GpuRoam
