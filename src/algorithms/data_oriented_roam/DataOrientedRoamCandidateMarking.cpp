#include "algorithms/data_oriented_roam/DataOrientedRoamCandidateMarking.h"
#include "algorithms/data_oriented_roam/DataOrientedRoamScoring.h"

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

    const DataOrientedRoamNodeIndex leftChild = state.Nodes.LeftChildAt(node);
    const DataOrientedRoamNodeIndex rightChild = state.Nodes.RightChildAt(node);
    if (!state.IsValidNode(leftChild) || !state.IsValidNode(rightChild) ||
        !state.IsLeaf(leftChild) || !state.IsLeaf(rightChild))
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

    const DataOrientedRoamNodeIndex baseNeighbor = state.Nodes.BaseNeighborAt(node);
    if (!state.IsValidNode(baseNeighbor) || state.IsLeaf(baseNeighbor))
    {
        return DataOrientedRoamMergeCandidateEvaluation{true, score, score};
    }

    // An internal opposite parent must form a mutual diamond whose children are
    // still leaves at the instant the topology transaction is committed.
    const DataOrientedRoamNodeIndex baseLeftChild = state.Nodes.LeftChildAt(baseNeighbor);
    const DataOrientedRoamNodeIndex baseRightChild = state.Nodes.RightChildAt(baseNeighbor);
    if (state.Nodes.BaseNeighborAt(baseNeighbor) != node ||
        !state.IsValidNode(baseLeftChild) ||
        !state.IsValidNode(baseRightChild) ||
        !state.IsLeaf(baseLeftChild) ||
        !state.IsLeaf(baseRightChild))
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
