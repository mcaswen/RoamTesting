#pragma once

#include "algorithms/cbt_2024/CbtBisectorTopology.h"

#include <cstdint>
#include <string>
#include <vector>

namespace ParallelRoam::Algorithms::Cbt2024
{
/// <summary>
/// PrepareSimplify、Simplify 和 PropagateSimplify 完成后的 CPU 拓扑快照
/// </summary>
struct CbtSimplifyCommitResult
{
    std::vector<std::uint64_t> HeapIds;
    std::vector<CbtBisectorNeighbors> Neighbors;
    std::vector<CbtBisectorData> BisectorData;
    std::vector<std::uint32_t> SimplificationNodes;
    std::vector<std::uint32_t> ReleasedDynamicSlots;
    std::vector<std::uint32_t> PropagationNodes;
    std::uint32_t PairMergeCount{0U};
    std::uint32_t QuadMergeCount{0U};
    bool Valid{true};
};

/// @brief 按上游顺序筛选唯一 merge 任务，提交两节点或四节点合并并传播邻接修补
[[nodiscard]] CbtSimplifyCommitResult CommitCbtSimplifications(
    const std::vector<std::uint64_t>& heapIds,
    const std::vector<CbtBisectorNeighbors>& neighbors,
    const std::vector<CbtBisectorData>& bisectorData,
    const std::vector<std::uint32_t>& simplifyCandidates,
    std::uint32_t dynamicElementCount);

/// @brief 检查释放槽、活动 heapID 和发布后邻接的范围及双向引用
[[nodiscard]] bool ValidateCbtSimplifiedTopology(
    const CbtSimplifyCommitResult& topology,
    std::uint32_t dynamicElementCount,
    std::string* errorMessage);
} // namespace ParallelRoam::Algorithms::Cbt2024
