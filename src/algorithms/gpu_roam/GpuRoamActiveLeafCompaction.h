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
    std::uint32_t ProgramId{0}; // 已链接的 compute program，由 builder 缓存
    std::uint32_t NodeBufferId{0}; // 稀疏节点池，只读
    std::uint32_t ActiveLeafBufferId{0}; // 稠密叶索引输出
    std::uint32_t CounterBufferId{0}; // ActiveLeafCount 的原子分配源
    std::size_t NodeCount{0}; // 本次允许扫描的节点上界
};

[[nodiscard]] bool EnsureGpuRoamActiveLeafCompactionProgram(
    std::uint32_t& programId,
    std::string* errorMessage);

void RunGpuRoamActiveLeafCompactionPass(const GpuRoamActiveLeafCompactionPassInput& input);
} // namespace ParallelRoam::Algorithms::GpuRoam
