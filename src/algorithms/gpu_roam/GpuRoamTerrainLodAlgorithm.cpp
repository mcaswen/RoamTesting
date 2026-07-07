#include "algorithms/gpu_roam/GpuRoamTerrainLodAlgorithm.h"

#include "algorithms/TerrainLodProfiling.h"
#include "algorithms/gpu_roam/GpuRoamBufferSchema.h"
#include "platform/OpenGlCapabilities.h"

#include <algorithm>
#include <chrono>

namespace ParallelRoam::Algorithms::GpuRoam
{
namespace
{
float ErrorEvaluationMilliseconds(const DataOrientedRoam::DataOrientedRoamStats& stats)
{
    return stats.ErrorEvaluationWorkerCount > 1U ?
        stats.ErrorEvaluationParallelMilliseconds :
        stats.ErrorEvaluationSingleThreadMilliseconds;
}

float CandidateCollectMilliseconds(const DataOrientedRoam::DataOrientedRoamStats& stats)
{
    return stats.ActiveLeafCollectMilliseconds +
           stats.SplitCandidateMarkMilliseconds +
           stats.MergeCandidateMarkMilliseconds;
}

DataOrientedRoam::DataOrientedRoamSettings ToDataOrientedSettings(const TerrainLodSettings& settings)
{
    DataOrientedRoam::DataOrientedRoamSettings dataSettings{};
    dataSettings.MaxDepth = settings.MaxDepth;
    dataSettings.SplitThreshold = settings.SplitThreshold;
    dataSettings.MergeThreshold = settings.MergeThreshold;
    dataSettings.DistanceScale = settings.DistanceScale;
    dataSettings.ErrorEvaluationWorkerCount = 0U;
    dataSettings.EnableLocalConstraints = settings.EnableLocalConstraints;
    dataSettings.EnableTopologyValidation = settings.EnableTopologyValidation;
    return dataSettings;
}

TerrainLodStats ToTerrainLodStats(const DataOrientedRoam::DataOrientedRoamStats& stats)
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
    lodStats.CpuWorkerCount = std::max({
        std::size_t{1},
        stats.ErrorEvaluationWorkerCount,
        stats.CollectWorkerCount,
        stats.CandidateMarkWorkerCount,
        stats.EmitWorkerCount,
        stats.TopologyCommitWorkerCount,
    });

    const float errorEvaluationMilliseconds = ErrorEvaluationMilliseconds(stats);
    const float splitCollectMilliseconds =
        stats.ActiveLeafCollectMilliseconds + stats.SplitCandidateMarkMilliseconds;
    lodStats.CpuUpdateMilliseconds = stats.UpdateMilliseconds;
    lodStats.CpuErrorEvalMilliseconds = errorEvaluationMilliseconds;
    lodStats.CpuDecisionMilliseconds =
        std::max(0.0F, stats.SplitMilliseconds - errorEvaluationMilliseconds - splitCollectMilliseconds);
    lodStats.CpuTopologyMilliseconds =
        lodStats.CpuDecisionMilliseconds + std::max(0.0F, stats.MergeMilliseconds - stats.MergeCandidateMarkMilliseconds);
    lodStats.CpuCollectMilliseconds = CandidateCollectMilliseconds(stats);
    lodStats.CpuMeshBuildMilliseconds = stats.EmitMilliseconds;
    lodStats.SplitMilliseconds = stats.SplitMilliseconds;
    lodStats.MergeMilliseconds = stats.MergeMilliseconds;
    lodStats.EmitMilliseconds = stats.EmitMilliseconds;
    lodStats.ValidateMilliseconds = stats.ValidateMilliseconds;
    lodStats.MaxActiveDepth = stats.MaxDepthReached;
    return lodStats;
}
} // namespace

TerrainLodAlgorithmInfo GpuRoamTerrainLodAlgorithm::Info() const
{
    return TerrainLodAlgorithmInfo{
        TerrainLodAlgorithmId::GpuRoamLike,
        "gpu_roam_like",
        "GPU ROAM-like",
        "GPU-oriented ROAM pipeline adapter with capability gate",
    };
}

TerrainLodAlgorithmCapabilities GpuRoamTerrainLodAlgorithm::Capabilities() const
{
    TerrainLodAlgorithmCapabilities capabilities{};
    const Platform::OpenGlGpuCapabilities gpuCapabilities = Platform::QueryOpenGlGpuCapabilities();
    capabilities.SupportsCpuMeshOutput = false;
    capabilities.SupportsGpuDrivenRendering = gpuCapabilities.SupportsGpuRoamCompute();
    capabilities.SupportsSplit = true;
    capabilities.SupportsMerge = true;
    capabilities.SupportsCrackFix = true;
    capabilities.SupportsTopologyValidation = true;
    return capabilities;
}

bool GpuRoamTerrainLodAlgorithm::BuildRenderData(
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
            *errorMessage = "GPU ROAM-like build failed: invalid height map";
        }
        return false;
    }

    const std::string unavailableReason = GpuRoamLikeUnavailableReason();
    if (!unavailableReason.empty())
    {
        if (errorMessage != nullptr)
        {
            *errorMessage = "GPU ROAM-like unavailable: " + unavailableReason;
        }
        return false;
    }

    const TerrainLodCpuSample cpuSampleStart = CaptureTerrainLodCpuSample();
    _cpuTopologyBuilder.UpdateTopology(
        *input.HeightMap,
        input.Settings.TerrainSize,
        input.Settings.HeightScale,
        input.CameraPosition,
        ToDataOrientedSettings(input.Settings));

    _stats = ToTerrainLodStats(_cpuTopologyBuilder.Stats());
    const auto snapshotStart = std::chrono::steady_clock::now();
    const GpuRoamBufferSnapshot snapshot = BuildGpuRoamBufferSnapshot(_cpuTopologyBuilder.State());
    _stats.GpuSnapshotBuildMilliseconds =
        std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - snapshotStart).count();
    if (!_gpuMeshBuilder.Build(snapshot, input, outPacket, _stats, errorMessage))
    {
        return false;
    }

    const TerrainLodCpuSample cpuSampleEnd = CaptureTerrainLodCpuSample();
    _stats.CpuUtilizationPercent = ComputeCpuUtilizationPercent(cpuSampleStart, cpuSampleEnd);
    return true;
}

const TerrainLodStats& GpuRoamTerrainLodAlgorithm::Stats() const
{
    return _stats;
}

void GpuRoamTerrainLodAlgorithm::Reset()
{
    _gpuMeshBuilder.Reset();
    _cpuTopologyBuilder = DataOrientedRoam::DataOrientedRoamMeshBuilder{};
    _stats = {};
}

std::string GpuRoamLikeUnavailableReason()
{
    const Platform::OpenGlGpuCapabilities capabilities = Platform::QueryOpenGlGpuCapabilities();
    const std::string capabilityReason = capabilities.GpuRoamComputeUnavailableReason();
    if (!capabilityReason.empty())
    {
        return capabilityReason;
    }

    return {};
}
} // namespace ParallelRoam::Algorithms::GpuRoam
