#include "algorithms/data_oriented_roam/DataOrientedRoamState.h"

#include <algorithm>

namespace ParallelRoam::Algorithms::DataOrientedRoam
{
namespace
{
DataOrientedRoamMergeCandidateEvaluation EvaluateMergeCandidateImpl(
    const DataOrientedRoamState& state,
    DataOrientedRoamNodeIndex node,
    float maximumScore)
{
    if (!state.IsValidNode(node) || state.IsLeaf(node))
    {
        return {};
    }

    const DataOrientedRoamNodeConstRef candidate = state.Nodes[node];
    if (!state.IsValidNode(candidate.LeftChild) || !state.IsValidNode(candidate.RightChild) ||
        !state.IsLeaf(candidate.LeftChild) || !state.IsLeaf(candidate.RightChild))
    {
        return {};
    }

    // Commit-time validation deliberately recomputes only the selected parent.
    // Persistent Q_m owns frame-wide scoring and topology membership discovery.
    const float score = ComputeScreenErrorScore(state, node);
    if (score > maximumScore)
    {
        return {};
    }

    const DataOrientedRoamNodeIndex baseNeighbor = candidate.BaseNeighbor;
    if (!state.IsValidNode(baseNeighbor) || state.IsLeaf(baseNeighbor))
    {
        return DataOrientedRoamMergeCandidateEvaluation{true, score, score};
    }

    // An internal opposite parent must form a mutual diamond whose children are
    // still leaves at the instant the topology transaction is committed.
    if (state.Nodes[baseNeighbor].BaseNeighbor != node ||
        !state.IsValidNode(state.Nodes[baseNeighbor].LeftChild) ||
        !state.IsValidNode(state.Nodes[baseNeighbor].RightChild) ||
        !state.IsLeaf(state.Nodes[baseNeighbor].LeftChild) ||
        !state.IsLeaf(state.Nodes[baseNeighbor].RightChild))
    {
        return {};
    }

    const float baseNeighborScore = ComputeScreenErrorScore(state, baseNeighbor);
    if (baseNeighborScore > maximumScore)
    {
        return {};
    }

    // PairScore is the loss of the complete diamond transaction and therefore the
    // same max(parent priorities) key used by persistent Q_m.
    return DataOrientedRoamMergeCandidateEvaluation{
        true,
        score,
        std::max(score, baseNeighborScore)};
}
} // namespace

DataOrientedRoamMergeCandidateEvaluation EvaluateMergeCandidate(
    const DataOrientedRoamState& state,
    DataOrientedRoamNodeIndex node,
    float maximumScore)
{
    return EvaluateMergeCandidateImpl(state, node, maximumScore);
}

bool CanMergeNode(const DataOrientedRoamState& state, DataOrientedRoamNodeIndex node)
{
    return CanMergeNode(state, node, state.Settings.MergeThreshold);
}

bool CanMergeNode(
    const DataOrientedRoamState& state,
    DataOrientedRoamNodeIndex node,
    float maximumScore)
{
    return EvaluateMergeCandidate(state, node, maximumScore).Eligible;
}
} // namespace ParallelRoam::Algorithms::DataOrientedRoam
