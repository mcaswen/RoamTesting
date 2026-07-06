#pragma once

#include "algorithms/data_oriented_roam/DataOrientedRoamState.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace ParallelRoam::Algorithms::GpuRoam
{
enum GpuRoamNodeFlags : std::uint32_t
{
    GpuRoamNodeFlagIsSplit = 1U << 0U,
    GpuRoamNodeFlagActivatedByForcedSplit = 1U << 1U,
    GpuRoamNodeFlagActiveLeaf = 1U << 2U,
};

/// <summary>
/// std430 node record，按 16 字节组打包 DOD SoA 节点字段
/// </summary>
struct GpuRoamNodeRecord
{
    float DomainAAndB[4]{};
    float DomainCAndErrors[4]{};
    std::uint32_t Topology0[4]{};
    std::uint32_t Topology1[4]{};
    std::uint32_t PathAndCreatedBuild[4]{};
    std::uint32_t ActivatedAndSplitBuild[4]{};
    std::uint32_t MergeBuildAndDepth[4]{};
};

/// <summary>
/// 单帧上传到 GPU 的 DOD 拓扑快照
/// </summary>
struct GpuRoamBufferSnapshot
{
    std::vector<GpuRoamNodeRecord> Nodes;
    std::vector<std::uint32_t> ActiveLeafIndices;
    int MaxDepth{0};
    int MaxDepthReached{0};

    [[nodiscard]] std::size_t NodeBufferBytes() const;
    [[nodiscard]] std::size_t ActiveLeafBufferBytes() const;
};

static_assert(sizeof(GpuRoamNodeRecord) % 16U == 0U);

[[nodiscard]] GpuRoamBufferSnapshot BuildGpuRoamBufferSnapshot(
    const DataOrientedRoam::DataOrientedRoamState& state);
} // namespace ParallelRoam::Algorithms::GpuRoam
