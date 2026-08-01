#include "algorithms/data_oriented_roam/DataOrientedRoamState.h"

namespace ParallelRoam::Algorithms::DataOrientedRoam
{
float EvaluateScreenErrorForNode(DataOrientedRoamState& state, DataOrientedRoamNodeIndex node)
{
    if (!state.IsValidNode(node))
    {
        return 0.0F;
    }

    // 融合扫描只覆盖进入 split pass 前的 active leaf。
    // split 过程中新增 child 走即时评分，保持级联细分使用同一公式。
    const DataOrientedRoamNodePool& nodes = state.Nodes;
    const float score = ComputeScreenErrorScore(state, nodes[node]);
    state.Nodes.ScreenErrors[node] = score;
    return score;
}
} // 命名空间 ParallelRoam::Algorithms::DataOrientedRoam
