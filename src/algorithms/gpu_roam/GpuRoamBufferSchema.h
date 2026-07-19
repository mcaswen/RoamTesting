#pragma once

#include "algorithms/data_oriented_roam/DataOrientedRoamState.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace ParallelRoam::Algorithms::GpuRoam
{
/// <summary>
/// GPU 节点记录中可组合的拓扑和活动状态位
/// </summary>
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
    float DomainAAndB[4]{}; // A.xy 和 B.xy 定义三角形前两个顶点
    float DomainCAndErrors[4]{}; // C.xy 加几何误差和上一帧屏幕误差
    std::uint32_t Topology0[4]{}; // parent、left child、right child、base neighbor
    std::uint32_t Topology1[4]{}; // 左右邻居、flags、预留字段
    std::uint32_t PathAndCreatedBuild[4]{}; // path code、root edge 和创建序列低位
    std::uint32_t ActivatedAndSplitBuild[4]{}; // 创建序列高位和最近激活分裂序列
    std::uint32_t MergeBuildAndDepth[4]{}; // 最近 merge 序列和节点深度
};

/// <summary>
/// 单帧上传到 GPU 的 DOD 拓扑快照
/// </summary>
struct GpuRoamBufferSnapshot
{
    std::vector<GpuRoamNodeRecord> Nodes;
    std::vector<std::uint32_t> ActiveLeafIndices;
    std::uint64_t BuildSequence{0};
    int MaxDepth{0};
    int MaxDepthReached{0};

    [[nodiscard]] std::size_t NodeBufferBytes() const;
    [[nodiscard]] std::size_t ActiveLeafBufferBytes() const;
};

static_assert(sizeof(GpuRoamNodeRecord) % 16U == 0U);

[[nodiscard]] GpuRoamBufferSnapshot BuildGpuRoamBufferSnapshot(
    const DataOrientedRoam::DataOrientedRoamState& state);
} // namespace ParallelRoam::Algorithms::GpuRoam
