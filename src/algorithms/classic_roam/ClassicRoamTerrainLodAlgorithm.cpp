#include "algorithms/classic_roam/ClassicRoamTerrainLodAlgorithm.h"

namespace ParallelRoam::Algorithms::ClassicRoam
{
TerrainLodAlgorithmInfo ClassicRoamTerrainLodAlgorithm::Info() const
{
    return TerrainLodAlgorithmInfo{
        TerrainLodAlgorithmId::ClassicCpuRoam,
        "classic_cpu_roam",
        "Classic CPU ROAM",
        "Object-style CPU ROAM baseline with persistent bintree topology",
    };
}

TerrainLodAlgorithmCapabilities ClassicRoamTerrainLodAlgorithm::Capabilities() const
{
    TerrainLodAlgorithmCapabilities capabilities{};
    capabilities.SupportsCpuMeshOutput = true;
    capabilities.SupportsSplit = true;
    capabilities.SupportsMerge = true;
    capabilities.SupportsCrackFix = true;
    capabilities.SupportsTopologyValidation = true;
    return capabilities;
}

bool ClassicRoamTerrainLodAlgorithm::BuildRenderData(
    const TerrainLodBuildInput& input,
    TerrainLodRenderPacket& outPacket,
    std::string* errorMessage)
{
    _stats = {};
    outPacket = {};

    if (input.HeightMap == nullptr || !input.HeightMap->IsValid())
    {
        if (errorMessage != nullptr)
        {
            *errorMessage = "Classic CPU ROAM build failed: invalid height map";
        }
        return false;
    }

    outPacket.Mode = TerrainLodRenderMode::CpuMesh;
    outPacket.CpuMesh = _builder.Build(
        *input.HeightMap,
        input.Settings.TerrainSize,
        input.Settings.HeightScale,
        input.CameraPosition,
        ToClassicSettings(input.Settings));
    _stats = ToTerrainLodStats(_builder.Stats());
    outPacket.ActiveTriangleCount = _stats.ActiveTriangleCount;
    outPacket.IndexCount = outPacket.CpuMesh.Indices.size();
    return !outPacket.CpuMesh.Vertices.empty() && !outPacket.CpuMesh.Indices.empty();
}

const TerrainLodStats& ClassicRoamTerrainLodAlgorithm::Stats() const
{
    return _stats;
}

void ClassicRoamTerrainLodAlgorithm::Reset()
{
    _builder = ClassicRoamMeshBuilder{};
    _stats = {};
}

ClassicRoamSettings ClassicRoamTerrainLodAlgorithm::ToClassicSettings(const TerrainLodSettings& settings)
{
    ClassicRoamSettings classicSettings{};
    classicSettings.MaxDepth = settings.MaxDepth;
    classicSettings.SplitThreshold = settings.SplitThreshold;
    classicSettings.MergeThreshold = settings.MergeThreshold;
    classicSettings.DistanceScale = settings.DistanceScale;
    classicSettings.SplitBudget = settings.SplitBudget;
    classicSettings.EnableLocalConstraints = settings.EnableLocalConstraints;
    classicSettings.EnableTopologyValidation = settings.EnableTopologyValidation;
    return classicSettings;
}

TerrainLodStats ClassicRoamTerrainLodAlgorithm::ToTerrainLodStats(const ClassicRoamStats& stats)
{
    TerrainLodStats lodStats{};
    lodStats.ActiveTriangleCount = stats.ActiveTriangleCount;
    lodStats.ActiveNodeCount = stats.NodeCount;
    lodStats.OriginalTriangleCount = stats.OriginalTriangleCount;
    lodStats.SubdividedTriangleCount = stats.SubdividedTriangleCount;
    lodStats.RebuiltTriangleCount = stats.RebuiltTriangleCount;
    lodStats.ActiveSplitCount = stats.ActiveSplitCount;
    lodStats.SplitCount = stats.SplitCount;
    lodStats.ForcedSplitCount = stats.ForcedSplitCount;
    lodStats.MergeCount = stats.MergeCount;
    lodStats.CrackRiskCount = stats.CrackRiskCount;
    lodStats.ConstraintPassCount = stats.ConstraintPassCount;
    lodStats.CandidatePeakCount = stats.CandidatePeakCount;
    lodStats.RejectedSplitCount = stats.RejectedSplitCount;
    lodStats.RejectedMergeCount = stats.RejectedMergeCount;
    lodStats.TjunctionCount = stats.TjunctionCount;
    lodStats.InvalidNeighborCount = stats.InvalidNeighborCount;
    lodStats.InvalidTopologyCount = stats.InvalidTopologyCount;
    lodStats.CpuUpdateMilliseconds = stats.UpdateMilliseconds;
    lodStats.CpuDecisionMilliseconds = stats.SplitMilliseconds;
    lodStats.CpuTopologyMilliseconds = stats.SplitMilliseconds + stats.MergeMilliseconds;
    lodStats.CpuMeshBuildMilliseconds = stats.EmitMilliseconds;
    lodStats.SplitMilliseconds = stats.SplitMilliseconds;
    lodStats.MergeMilliseconds = stats.MergeMilliseconds;
    lodStats.EmitMilliseconds = stats.EmitMilliseconds;
    lodStats.ValidateMilliseconds = stats.ValidateMilliseconds;
    lodStats.MaxActiveDepth = stats.MaxDepthReached;
    return lodStats;
}
} // 命名空间 ParallelRoam::Algorithms::ClassicRoam
