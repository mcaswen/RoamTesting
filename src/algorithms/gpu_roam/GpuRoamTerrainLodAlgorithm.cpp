#include "algorithms/gpu_roam/GpuRoamTerrainLodAlgorithm.h"

#include "algorithms/TerrainLodProfiling.h"
#include "algorithms/gpu_roam/GpuRoamBufferSchema.h"
#include "platform/OpenGlCapabilities.h"

#include <glad/gl.h>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <vector>

namespace ParallelRoam::Algorithms::GpuRoam
{
namespace
{
using Clock = std::chrono::steady_clock;

std::string BuildStagingStatusMessage()
{
    return "GPU ROAM-like 4B staging: CPU DOD topology and CPU mesh fallback are active, GPU node buffers are uploaded, compute/render are not active yet";
}

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

bool UploadBuffer(
    GLenum target,
    std::uint32_t& bufferId,
    const void* data,
    std::size_t byteCount,
    std::string* errorMessage)
{
    if (bufferId == 0U)
    {
        GLuint nextBufferId = 0U;
        glGenBuffers(1, &nextBufferId);
        bufferId = nextBufferId;
    }

    if (bufferId == 0U)
    {
        if (errorMessage != nullptr)
        {
            *errorMessage = "GPU ROAM-like buffer allocation failed";
        }
        return false;
    }

    glBindBuffer(target, bufferId);
    glBufferData(target, static_cast<GLsizeiptr>(byteCount), data, GL_DYNAMIC_DRAW);
    glBindBuffer(target, 0);
    return true;
}

bool UploadHeightMapTexture(
    const Terrain::HeightMap& heightMap,
    std::uint32_t& textureId,
    std::size_t& uploadBytes,
    std::string* errorMessage)
{
    if (textureId == 0U)
    {
        GLuint nextTextureId = 0U;
        glGenTextures(1, &nextTextureId);
        textureId = nextTextureId;
    }

    if (textureId == 0U)
    {
        if (errorMessage != nullptr)
        {
            *errorMessage = "GPU ROAM-like height map texture allocation failed";
        }
        return false;
    }

    std::vector<float> heights;
    heights.resize(static_cast<std::size_t>(heightMap.Width()) * static_cast<std::size_t>(heightMap.Height()));
    for (int y = 0; y < heightMap.Height(); ++y)
    {
        for (int x = 0; x < heightMap.Width(); ++x)
        {
            const auto index = static_cast<std::size_t>(y) * static_cast<std::size_t>(heightMap.Width()) +
                               static_cast<std::size_t>(x);
            heights[index] = heightMap.SamplePixel(x, y);
        }
    }

    glBindTexture(GL_TEXTURE_2D, textureId);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(
        GL_TEXTURE_2D,
        0,
        GL_R32F,
        heightMap.Width(),
        heightMap.Height(),
        0,
        GL_RED,
        GL_FLOAT,
        heights.data());
    glBindTexture(GL_TEXTURE_2D, 0);
    uploadBytes += heights.size() * sizeof(float);
    return true;
}
} // namespace

GpuRoamTerrainLodAlgorithm::~GpuRoamTerrainLodAlgorithm()
{
    DestroyGpuResources();
}

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
    // 4B 仍用 DOD CPU topology 和 CPU mesh fallback，GPU 只持有快照 buffer
    capabilities.SupportsCpuMeshOutput = true;
    capabilities.SupportsGpuDrivenRendering = false;
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
    outPacket.Mode = TerrainLodRenderMode::CpuMesh;

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
    outPacket.CpuMesh = _cpuTopologyBuilder.Build(
        *input.HeightMap,
        input.Settings.TerrainSize,
        input.Settings.HeightScale,
        input.CameraPosition,
        ToDataOrientedSettings(input.Settings));

    const GpuRoamBufferSnapshot snapshot = BuildGpuRoamBufferSnapshot(_cpuTopologyBuilder.State());
    std::size_t uploadBytes = 0U;
    const auto uploadStart = Clock::now();
    if (!UploadBuffer(
            GL_SHADER_STORAGE_BUFFER,
            _nodeBufferId,
            snapshot.Nodes.data(),
            snapshot.NodeBufferBytes(),
            errorMessage))
    {
        return false;
    }
    uploadBytes += snapshot.NodeBufferBytes();

    if (!UploadBuffer(
            GL_SHADER_STORAGE_BUFFER,
            _activeLeafBufferId,
            snapshot.ActiveLeafIndices.data(),
            snapshot.ActiveLeafBufferBytes(),
            errorMessage))
    {
        return false;
    }
    uploadBytes += snapshot.ActiveLeafBufferBytes();

    if (!UploadHeightMapTexture(*input.HeightMap, _heightMapTextureId, uploadBytes, errorMessage))
    {
        return false;
    }
    const auto uploadEnd = Clock::now();
    const TerrainLodCpuSample cpuSampleEnd = CaptureTerrainLodCpuSample();

    _stats = ToTerrainLodStats(_cpuTopologyBuilder.Stats());
    _stats.CpuGpuUploadBytes = uploadBytes;
    _stats.CpuUploadMilliseconds =
        std::chrono::duration<float, std::milli>(uploadEnd - uploadStart).count();
    _stats.CpuUtilizationPercent = ComputeCpuUtilizationPercent(cpuSampleStart, cpuSampleEnd);

    outPacket.StatusMessage = BuildStagingStatusMessage();
    outPacket.GpuNodeBufferId = _nodeBufferId;
    outPacket.GpuHeightMapTextureId = _heightMapTextureId;
    outPacket.ActiveLeafBufferId = _activeLeafBufferId;
    outPacket.ActiveLeafCount = snapshot.ActiveLeafIndices.size();
    outPacket.ActiveTriangleCount = _stats.ActiveTriangleCount;
    outPacket.IndexCount = outPacket.CpuMesh.Indices.size();
    return !outPacket.CpuMesh.Vertices.empty() && !outPacket.CpuMesh.Indices.empty();
}

const TerrainLodStats& GpuRoamTerrainLodAlgorithm::Stats() const
{
    return _stats;
}

void GpuRoamTerrainLodAlgorithm::Reset()
{
    DestroyGpuResources();
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

void GpuRoamTerrainLodAlgorithm::DestroyGpuResources()
{
    if (_nodeBufferId != 0U && glad_glDeleteBuffers != nullptr)
    {
        const GLuint bufferId = _nodeBufferId;
        glDeleteBuffers(1, &bufferId);
        _nodeBufferId = 0U;
    }

    if (_activeLeafBufferId != 0U && glad_glDeleteBuffers != nullptr)
    {
        const GLuint bufferId = _activeLeafBufferId;
        glDeleteBuffers(1, &bufferId);
        _activeLeafBufferId = 0U;
    }

    if (_heightMapTextureId != 0U && glad_glDeleteTextures != nullptr)
    {
        const GLuint textureId = _heightMapTextureId;
        glDeleteTextures(1, &textureId);
        _heightMapTextureId = 0U;
    }
}
} // namespace ParallelRoam::Algorithms::GpuRoam
