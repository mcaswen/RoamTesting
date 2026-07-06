#include "algorithms/gpu_roam/GpuRoamBufferSchema.h"

#include <algorithm>
#include <unordered_set>

namespace ParallelRoam::Algorithms::GpuRoam
{
namespace
{
std::uint32_t Low32(std::uint64_t value)
{
    return static_cast<std::uint32_t>(value & 0xFFFFFFFFULL);
}

std::uint32_t High32(std::uint64_t value)
{
    return static_cast<std::uint32_t>(value >> 32U);
}
} // namespace

std::size_t GpuRoamBufferSnapshot::NodeBufferBytes() const
{
    return Nodes.size() * sizeof(GpuRoamNodeRecord);
}

std::size_t GpuRoamBufferSnapshot::ActiveLeafBufferBytes() const
{
    return ActiveLeafIndices.size() * sizeof(std::uint32_t);
}

GpuRoamBufferSnapshot BuildGpuRoamBufferSnapshot(
    const DataOrientedRoam::DataOrientedRoamState& state)
{
    GpuRoamBufferSnapshot snapshot{};
    snapshot.BuildSequence = state.BuildSequence;
    snapshot.MaxDepth = state.Settings.MaxDepth;
    snapshot.MaxDepthReached = state.Stats.MaxDepthReached;

    const std::size_t nodeCount = state.Nodes.size();
    snapshot.Nodes.resize(nodeCount);
    snapshot.ActiveLeafIndices.reserve(state.FinalActiveLeaves.size());

    std::unordered_set<DataOrientedRoam::DataOrientedRoamNodeIndex> activeLeafSet;
    activeLeafSet.reserve(state.FinalActiveLeaves.size());
    for (DataOrientedRoam::DataOrientedRoamNodeIndex leaf : state.FinalActiveLeaves)
    {
        if (leaf != DataOrientedRoam::InvalidDataOrientedRoamNodeIndex)
        {
            activeLeafSet.insert(leaf);
            snapshot.ActiveLeafIndices.push_back(static_cast<std::uint32_t>(leaf));
        }
    }

    for (DataOrientedRoam::DataOrientedRoamNodeIndex node = 0; node < nodeCount; ++node)
    {
        const DataOrientedRoam::DataOrientedRoamNodeConstRef source = state.Nodes[node];
        GpuRoamNodeRecord& target = snapshot.Nodes[node];

        target.DomainAAndB[0] = source.Domain.A.x;
        target.DomainAAndB[1] = source.Domain.A.y;
        target.DomainAAndB[2] = source.Domain.B.x;
        target.DomainAAndB[3] = source.Domain.B.y;

        target.DomainCAndErrors[0] = source.Domain.C.x;
        target.DomainCAndErrors[1] = source.Domain.C.y;
        target.DomainCAndErrors[2] = source.GeometricError;
        target.DomainCAndErrors[3] = source.ScreenError;

        target.Topology0[0] = source.Parent;
        target.Topology0[1] = source.LeftChild;
        target.Topology0[2] = source.RightChild;
        target.Topology0[3] = source.BaseNeighbor;

        std::uint32_t flags = 0U;
        if (source.IsSplit != 0U)
        {
            flags |= GpuRoamNodeFlagIsSplit;
        }
        if (source.ActivatedByForcedSplit != 0U)
        {
            flags |= GpuRoamNodeFlagActivatedByForcedSplit;
        }
        if (activeLeafSet.find(node) != activeLeafSet.end())
        {
            flags |= GpuRoamNodeFlagActiveLeaf;
        }

        target.Topology1[0] = source.LeftNeighbor;
        target.Topology1[1] = source.RightNeighbor;
        target.Topology1[2] = source.InteriorChunkId;
        target.Topology1[3] = flags;

        target.PathAndCreatedBuild[0] = Low32(source.PathId);
        target.PathAndCreatedBuild[1] = High32(source.PathId);
        target.PathAndCreatedBuild[2] = Low32(source.CreatedBuildId);
        target.PathAndCreatedBuild[3] = High32(source.CreatedBuildId);

        target.ActivatedAndSplitBuild[0] = Low32(source.ActivatedBuildId);
        target.ActivatedAndSplitBuild[1] = High32(source.ActivatedBuildId);
        target.ActivatedAndSplitBuild[2] = Low32(source.SplitBuildId);
        target.ActivatedAndSplitBuild[3] = High32(source.SplitBuildId);

        target.MergeBuildAndDepth[0] = Low32(source.MergeBuildId);
        target.MergeBuildAndDepth[1] = High32(source.MergeBuildId);
        target.MergeBuildAndDepth[2] = static_cast<std::uint32_t>(std::max(source.Depth, 0));
        target.MergeBuildAndDepth[3] = 0U;
    }

    return snapshot;
}
} // namespace ParallelRoam::Algorithms::GpuRoam
