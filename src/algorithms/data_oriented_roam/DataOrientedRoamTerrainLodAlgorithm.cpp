#include "algorithms/data_oriented_roam/DataOrientedRoamTerrainLodAlgorithm.h"

namespace ParallelRoam::Algorithms::DataOrientedRoam
{
TerrainLodAlgorithmInfo DataOrientedRoamTerrainLodAlgorithm::Info() const
{
    return TerrainLodAlgorithmInfo{
        TerrainLodAlgorithmId::DataOrientedCpuRoam,
        "data_oriented_cpu_roam",
        "Data-Oriented CPU ROAM",
        "Index-based CPU ROAM node pool baseline for Data-Oriented refactoring",
    };
}

TerrainLodAlgorithmCapabilities DataOrientedRoamTerrainLodAlgorithm::Capabilities() const
{
    TerrainLodAlgorithmCapabilities capabilities{};
    capabilities.SupportsCpuMeshOutput = true;
    capabilities.SupportsSplit = true;
    capabilities.SupportsMerge = true;
    capabilities.SupportsCrackFix = true;
    capabilities.SupportsTopologyValidation = true;
    return capabilities;
}

bool DataOrientedRoamTerrainLodAlgorithm::BuildRenderData(
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
            *errorMessage = "Data-Oriented CPU ROAM build failed: invalid height map";
        }
        return false;
    }

    outPacket.Mode = TerrainLodRenderMode::CpuMesh;
    outPacket.CpuMesh = _builder.Build(
        *input.HeightMap,
        input.Settings.TerrainSize,
        input.Settings.HeightScale,
        input.CameraPosition,
        ToDataOrientedSettings(input.Settings));
    _stats = ToTerrainLodStats(_builder.Stats());
    outPacket.ActiveTriangleCount = _stats.ActiveTriangleCount;
    outPacket.IndexCount = outPacket.CpuMesh.Indices.size();
    return !outPacket.CpuMesh.Vertices.empty() && !outPacket.CpuMesh.Indices.empty();
}

const TerrainLodStats& DataOrientedRoamTerrainLodAlgorithm::Stats() const
{
    return _stats;
}

void DataOrientedRoamTerrainLodAlgorithm::Reset()
{
    _builder = DataOrientedRoamMeshBuilder{};
    _stats = {};
}

DataOrientedRoamSettings DataOrientedRoamTerrainLodAlgorithm::ToDataOrientedSettings(
    const TerrainLodSettings& settings)
{
    DataOrientedRoamSettings dataSettings{};
    dataSettings.MaxDepth = settings.MaxDepth;
    dataSettings.SplitThreshold = settings.SplitThreshold;
    dataSettings.MergeThreshold = settings.MergeThreshold;
    dataSettings.DistanceScale = settings.DistanceScale;
    dataSettings.SplitBudget = settings.SplitBudget;
    dataSettings.EnableLocalConstraints = settings.EnableLocalConstraints;
    dataSettings.EnableTopologyValidation = settings.EnableTopologyValidation;
    return dataSettings;
}

TerrainLodStats DataOrientedRoamTerrainLodAlgorithm::ToTerrainLodStats(const DataOrientedRoamStats& stats)
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
} // 命名空间 ParallelRoam::Algorithms::DataOrientedRoam
