#pragma once

#include "algorithms/cbt_2024/CbtBisectorTopology.h"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace ParallelRoam::Algorithms::Cbt2024
{
/// <summary>
/// 四种官方 Bisect 模板在 CPU 上提交后的完整拓扑快照。
/// </summary>
struct CbtBisectCommitResult
{
    std::vector<std::uint64_t> HeapIds;
    std::vector<CbtBisectorNeighbors> Neighbors;
    std::vector<CbtBisectorData> BisectorData;
    std::vector<std::uint32_t> CommittedDynamicSlots;
    std::vector<std::uint32_t> PropagationNodes;
    // 顺序为 center、right-double、left-double、triple。
    std::array<std::uint32_t, 4> TemplateCounts{};
    bool Valid{true};
};

/// <summary>
/// LEB heapID 对应的基础面、当前子三角形和父三角形。
/// </summary>
struct CbtLebTriangleResult
{
    std::array<CbtBaseControlPoint, 3> Child{};
    std::array<CbtBaseControlPoint, 3> Parent{};
    std::uint32_t BaseBisector{0U};
    bool Valid{true};
};

/// @brief 复制当前邻接，执行 BisectElement，再执行全部 PropagateBisectElement。
[[nodiscard]] CbtBisectCommitResult CommitCbtBisects(
    const std::vector<std::uint64_t>& heapIds,
    const std::vector<CbtBisectorNeighbors>& currentNeighbors,
    const std::vector<CbtBisectorData>& bisectorData,
    const std::vector<std::uint32_t>& allocationNodes,
    std::uint32_t dynamicElementCount);

/// @brief 检查 heapID、动态槽位和三邻接的范围及双向引用。
[[nodiscard]] bool ValidateCbtCommittedTopology(
    const CbtBisectCommitResult& topology,
    std::uint32_t dynamicElementCount,
    std::string* errorMessage);

/// @brief 按上游 LEB splitting matrix 的等价中点递推解码平面三角形。
[[nodiscard]] CbtLebTriangleResult EvaluateCbtLebTriangle(
    std::uint64_t heapId,
    std::uint32_t baseDepth,
    const std::array<CbtBaseControlPoint, CbtBaseControlPointCount>& baseControlPoints);
} // namespace ParallelRoam::Algorithms::Cbt2024
