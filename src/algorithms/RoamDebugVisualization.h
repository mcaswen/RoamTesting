#pragma once

#include "algorithms/RoamDebugVisualization.shared.h"

#include <glm/vec3.hpp>

namespace ParallelRoam::Algorithms::Roam
{
/// <summary>
/// 返回当前 Build 由 split 新激活的最终叶节点颜色。
/// Classic 与 DOD 共用该值，防止算法和 UI 图例分别维护颜色语义。
/// </summary>
[[nodiscard]] inline glm::vec3 SplitDebugColor()
{
    return glm::vec3{
        ROAM_DEBUG_SPLIT_RED,
        ROAM_DEBUG_SPLIT_GREEN,
        ROAM_DEBUG_SPLIT_BLUE};
}

/// <summary>
/// 返回当前 Build 由 merge 恢复的最终父叶节点颜色。
/// </summary>
[[nodiscard]] inline glm::vec3 MergeDebugColor()
{
    return glm::vec3{
        ROAM_DEBUG_MERGE_RED,
        ROAM_DEBUG_MERGE_GREEN,
        ROAM_DEBUG_MERGE_BLUE};
}
} // namespace ParallelRoam::Algorithms::Roam
