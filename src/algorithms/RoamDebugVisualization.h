#pragma once

#include <glm/vec3.hpp>

namespace ParallelRoam::Algorithms::Roam
{
/// <summary>
/// 返回当前 Build 由 split 新激活的最终叶节点颜色。
/// Classic 与 DOD 共用该值，防止算法和 UI 图例分别维护颜色语义。
/// </summary>
[[nodiscard]] inline glm::vec3 SplitDebugColor()
{
    return glm::vec3{0.95F, 0.08F, 0.06F};
}

/// <summary>
/// 返回当前 Build 由 merge 恢复的最终父叶节点颜色。
/// </summary>
[[nodiscard]] inline glm::vec3 MergeDebugColor()
{
    return glm::vec3{0.10F, 0.88F, 0.18F};
}
} // namespace ParallelRoam::Algorithms::Roam
