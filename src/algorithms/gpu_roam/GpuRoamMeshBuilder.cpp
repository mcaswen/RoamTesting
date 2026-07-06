#include "algorithms/gpu_roam/GpuRoamMeshBuilder.h"

#include "algorithms/gpu_roam/GpuRoamActiveLeafCompaction.h"
#include "algorithms/gpu_roam/GpuRoamBufferSchema.h"
#include "algorithms/gpu_roam/GpuRoamCandidateMarking.h"
#include "algorithms/gpu_roam/GpuRoamErrorEvaluation.h"
#include "algorithms/gpu_roam/GpuRoamMeshEmit.h"
#include "algorithms/gpu_roam/GpuRoamSplitOnlyTopology.h"
#include "platform/OpenGlCapabilities.h"

#include <glad/gl.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <sstream>
#include <string>
#include <vector>

namespace ParallelRoam::Algorithms::GpuRoam
{
namespace
{
using Clock = std::chrono::steady_clock;
constexpr std::size_t GpuRoamDebugSampleCount = 8U;

std::string BuildGpuStatusMessage(bool usesIndirectDraw)
{
    if (usesIndirectDraw)
    {
        return "GPU ROAM-like: CPU DOD baseline, GPU split-only topology, mesh emit and indirect draw";
    }

    return "GPU ROAM-like: CPU DOD baseline, GPU split-only topology, mesh emit and GPU buffer draw";
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

bool UploadBufferWithCapacity(
    GLenum target,
    std::uint32_t& bufferId,
    const void* data,
    std::size_t dataByteCount,
    std::size_t capacityByteCount,
    std::string* errorMessage)
{
    if (bufferId == 0U)
    {
        GLuint nextBufferId = 0U;
        glGenBuffers(1, &nextBufferId);
        bufferId = nextBufferId;
    }

    if (bufferId == 0U || dataByteCount > capacityByteCount)
    {
        if (errorMessage != nullptr)
        {
            *errorMessage = "GPU ROAM-like buffer allocation failed";
        }
        return false;
    }

    glBindBuffer(target, bufferId);
    glBufferData(target, static_cast<GLsizeiptr>(capacityByteCount), nullptr, GL_DYNAMIC_DRAW);
    if (data != nullptr && dataByteCount > 0U)
    {
        glBufferSubData(target, 0, static_cast<GLsizeiptr>(dataByteCount), data);
    }
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

bool GpuRoamMeshBuilder::Build(
    const GpuRoamBufferSnapshot& snapshot,
    const TerrainLodBuildInput& input,
    TerrainLodRenderPacket& outPacket,
    TerrainLodStats& inOutStats,
    std::string* errorMessage)
{
    const std::size_t gpuNodeCapacity = snapshot.Nodes.size() + snapshot.ActiveLeafIndices.size() * 2U;
    std::size_t uploadBytes = 0U;
    float cpuUploadMilliseconds = 0.0F;
    if (!UploadSnapshot(snapshot, *input.HeightMap, gpuNodeCapacity, uploadBytes, cpuUploadMilliseconds, errorMessage))
    {
        return false;
    }

    float gpuComputeMilliseconds = 0.0F;
    std::size_t readbackBytes = 0U;
    std::size_t gpuActiveLeafCount = snapshot.ActiveLeafIndices.size();
    std::size_t gpuNodeCount = snapshot.Nodes.size();
    std::size_t gpuSplitOnlyCommitCount = 0U;
    if (!RunGpuComputePipeline(
            snapshot,
            input,
            uploadBytes,
            gpuComputeMilliseconds,
            readbackBytes,
            gpuActiveLeafCount,
            gpuNodeCount,
            gpuSplitOnlyCommitCount,
            errorMessage))
    {
        return false;
    }

    inOutStats.CpuGpuUploadBytes = uploadBytes;
    inOutStats.CpuGpuReadbackBytes = readbackBytes;
    inOutStats.GpuComputeMilliseconds = gpuComputeMilliseconds;
    inOutStats.CpuUploadMilliseconds = cpuUploadMilliseconds;
    if (gpuSplitOnlyCommitCount > 0U)
    {
        inOutStats.ActiveTriangleCount = gpuActiveLeafCount;
        inOutStats.ActiveNodeCount = std::max(inOutStats.ActiveNodeCount, gpuNodeCount);
        inOutStats.SplitCount += gpuSplitOnlyCommitCount;
    }

    const Platform::OpenGlGpuCapabilities gpuCapabilities = Platform::QueryOpenGlGpuCapabilities();
    const bool usesIndirectDraw = gpuCapabilities.SupportsIndirectDraw && _state.IndirectDrawBufferId != 0U;
    outPacket.Mode = usesIndirectDraw ? TerrainLodRenderMode::GpuIndirect : TerrainLodRenderMode::GpuBuffers;
    outPacket.StatusMessage = BuildGpuStatusMessage(usesIndirectDraw);
    outPacket.GpuNodeBufferId = _state.NodeBufferId;
    outPacket.GpuHeightMapTextureId = _state.HeightMapTextureId;
    outPacket.GpuVertexBufferId = _state.GpuVertexBufferId;
    outPacket.GpuIndexBufferId = _state.GpuIndexBufferId;
    outPacket.ActiveLeafBufferId = _state.ActiveLeafBufferId;
    outPacket.IndirectDrawBufferId = usesIndirectDraw ? _state.IndirectDrawBufferId : 0U;
    outPacket.ActiveLeafCount = gpuActiveLeafCount;
    outPacket.ActiveTriangleCount = inOutStats.ActiveTriangleCount;
    outPacket.IndexCount = gpuActiveLeafCount * 3U;
    return outPacket.ActiveTriangleCount > 0U &&
           outPacket.IndexCount > 0U &&
           outPacket.GpuVertexBufferId != 0U &&
           outPacket.GpuIndexBufferId != 0U &&
           (outPacket.Mode != TerrainLodRenderMode::GpuIndirect || outPacket.IndirectDrawBufferId != 0U);
}

void GpuRoamMeshBuilder::Reset()
{
    _state.Reset();
}

bool GpuRoamMeshBuilder::UploadSnapshot(
    const GpuRoamBufferSnapshot& snapshot,
    const Terrain::HeightMap& heightMap,
    std::size_t nodeCapacity,
    std::size_t& uploadBytes,
    float& cpuUploadMilliseconds,
    std::string* errorMessage)
{
    const auto uploadStart = Clock::now();
    if (!UploadBufferWithCapacity(
            GL_SHADER_STORAGE_BUFFER,
            _state.NodeBufferId,
            snapshot.Nodes.data(),
            snapshot.NodeBufferBytes(),
            nodeCapacity * sizeof(GpuRoamNodeRecord),
            errorMessage))
    {
        return false;
    }
    uploadBytes += snapshot.NodeBufferBytes();

    if (!UploadBuffer(
            GL_SHADER_STORAGE_BUFFER,
            _state.ActiveLeafBufferId,
            nullptr,
            nodeCapacity * sizeof(std::uint32_t),
            errorMessage))
    {
        return false;
    }

    if (!UploadHeightMapTexture(heightMap, _state.HeightMapTextureId, uploadBytes, errorMessage))
    {
        return false;
    }
    const auto uploadEnd = Clock::now();
    cpuUploadMilliseconds = std::chrono::duration<float, std::milli>(uploadEnd - uploadStart).count();
    return true;
}

bool GpuRoamMeshBuilder::RunGpuComputePipeline(
    const GpuRoamBufferSnapshot& snapshot,
    const TerrainLodBuildInput& input,
    std::size_t& uploadBytes,
    float& gpuComputeMilliseconds,
    std::size_t& readbackBytes,
    std::size_t& gpuActiveLeafCount,
    std::size_t& gpuNodeCount,
    std::size_t& gpuSplitOnlyCommitCount,
    std::string* errorMessage)
{
    if (!EnsureGpuRoamActiveLeafCompactionProgram(_state.ActiveLeafCompactionProgramId, errorMessage) ||
        !EnsureGpuRoamErrorEvaluationProgram(_state.ErrorEvaluationProgramId, errorMessage) ||
        !EnsureGpuRoamCandidateMarkingProgram(_state.CandidateMarkingProgramId, errorMessage) ||
        !EnsureGpuRoamMeshEmitProgram(_state.MeshEmitProgramId, errorMessage))
    {
        return false;
    }

    const std::size_t nodeCount = snapshot.Nodes.size();
    const std::size_t activeLeafCount = snapshot.ActiveLeafIndices.size();
    const std::size_t nodeCapacity = nodeCount + activeLeafCount * 2U;
    const std::size_t activeLeafCapacity = std::max<std::size_t>(activeLeafCount * 2U, activeLeafCount);
    const std::size_t screenErrorBytes = activeLeafCount * sizeof(float);
    const std::size_t candidateBufferBytes = std::max<std::size_t>(nodeCapacity, 1U) * sizeof(std::uint32_t);
    const std::size_t vertexBufferBytes =
        activeLeafCapacity * 3U * sizeof(Terrain::TerrainMeshVertex);
    const std::size_t indexBufferBytes =
        activeLeafCapacity * 3U * sizeof(std::uint32_t);
    const GpuRoamDrawElementsIndirectCommand emptyIndirectCommand{};

    if (!UploadBuffer(GL_SHADER_STORAGE_BUFFER, _state.ScreenErrorBufferId, nullptr, screenErrorBytes, errorMessage) ||
        !UploadBuffer(GL_SHADER_STORAGE_BUFFER, _state.SplitCandidateBufferId, nullptr, candidateBufferBytes, errorMessage) ||
        !UploadBuffer(GL_SHADER_STORAGE_BUFFER, _state.MergeCandidateBufferId, nullptr, candidateBufferBytes, errorMessage) ||
        !UploadBuffer(GL_SHADER_STORAGE_BUFFER, _state.GpuVertexBufferId, nullptr, vertexBufferBytes, errorMessage) ||
        !UploadBuffer(GL_SHADER_STORAGE_BUFFER, _state.GpuIndexBufferId, nullptr, indexBufferBytes, errorMessage) ||
        !UploadBuffer(
            GL_SHADER_STORAGE_BUFFER,
            _state.IndirectDrawBufferId,
            &emptyIndirectCommand,
            sizeof(emptyIndirectCommand),
            errorMessage))
    {
        return false;
    }

    GpuRoamCounters zeroCounters{};
    zeroCounters.AllocatedNodeCount = static_cast<std::uint32_t>(nodeCount);
    if (!UploadBuffer(GL_SHADER_STORAGE_BUFFER, _state.CounterBufferId, &zeroCounters, sizeof(zeroCounters), errorMessage))
    {
        return false;
    }
    uploadBytes += sizeof(zeroCounters);
    uploadBytes += sizeof(emptyIndirectCommand);

    if (_state.TimerQueryId == 0U)
    {
        GLuint queryId = 0U;
        glGenQueries(1, &queryId);
        _state.TimerQueryId = queryId;
    }

    glBeginQuery(GL_TIME_ELAPSED, _state.TimerQueryId);

    GpuRoamActiveLeafCompactionPassInput compactionInput{};
    compactionInput.ProgramId = _state.ActiveLeafCompactionProgramId;
    compactionInput.NodeBufferId = _state.NodeBufferId;
    compactionInput.ActiveLeafBufferId = _state.ActiveLeafBufferId;
    compactionInput.CounterBufferId = _state.CounterBufferId;
    compactionInput.NodeCount = nodeCount;
    RunGpuRoamActiveLeafCompactionPass(compactionInput);

    GpuRoamErrorEvaluationPassInput errorInput{};
    errorInput.ProgramId = _state.ErrorEvaluationProgramId;
    errorInput.NodeBufferId = _state.NodeBufferId;
    errorInput.ActiveLeafBufferId = _state.ActiveLeafBufferId;
    errorInput.ScreenErrorBufferId = _state.ScreenErrorBufferId;
    errorInput.HeightMapTextureId = _state.HeightMapTextureId;
    errorInput.ActiveLeafCount = activeLeafCount;
    errorInput.TerrainSize = input.Settings.TerrainSize;
    errorInput.HeightScale = input.Settings.HeightScale;
    errorInput.DistanceScale = input.Settings.DistanceScale;
    errorInput.CameraPosition = input.CameraPosition;
    RunGpuRoamErrorEvaluationPass(errorInput);

    GpuRoamCandidateMarkingPassInput candidateInput{};
    candidateInput.ProgramId = _state.CandidateMarkingProgramId;
    candidateInput.NodeBufferId = _state.NodeBufferId;
    candidateInput.ActiveLeafBufferId = _state.ActiveLeafBufferId;
    candidateInput.ScreenErrorBufferId = _state.ScreenErrorBufferId;
    candidateInput.CounterBufferId = _state.CounterBufferId;
    candidateInput.SplitCandidateBufferId = _state.SplitCandidateBufferId;
    candidateInput.MergeCandidateBufferId = _state.MergeCandidateBufferId;
    candidateInput.HeightMapTextureId = _state.HeightMapTextureId;
    candidateInput.NodeCount = nodeCount;
    candidateInput.ActiveLeafLimit = activeLeafCount;
    candidateInput.MaxDepth = input.Settings.MaxDepth;
    candidateInput.TerrainSize = input.Settings.TerrainSize;
    candidateInput.HeightScale = input.Settings.HeightScale;
    candidateInput.DistanceScale = input.Settings.DistanceScale;
    candidateInput.SplitThreshold = input.Settings.SplitThreshold;
    candidateInput.MergeThreshold = input.Settings.MergeThreshold;
    candidateInput.CameraPosition = input.CameraPosition;
    RunGpuRoamCandidateMarkingPass(candidateInput);

    GpuRoamSplitOnlyTopologyPassInput splitOnlyInput{};
    splitOnlyInput.NodeBufferId = _state.NodeBufferId;
    splitOnlyInput.SplitCandidateBufferId = _state.SplitCandidateBufferId;
    splitOnlyInput.CounterBufferId = _state.CounterBufferId;
    splitOnlyInput.CandidateDispatchCount = activeLeafCount;
    splitOnlyInput.NodeCapacity = nodeCapacity;
    splitOnlyInput.MaxDepth = input.Settings.MaxDepth;
    splitOnlyInput.BuildSequence = snapshot.BuildSequence;
    if (!RunGpuRoamSplitOnlyTopologyPass(_state.SplitOnlyTopologyProgramId, splitOnlyInput, errorMessage))
    {
        return false;
    }

    const std::uint32_t zeroActiveLeafCount = 0U;
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, _state.CounterBufferId);
    glBufferSubData(
        GL_SHADER_STORAGE_BUFFER,
        offsetof(GpuRoamCounters, ActiveLeafCount),
        static_cast<GLsizeiptr>(sizeof(zeroActiveLeafCount)),
        &zeroActiveLeafCount);
    uploadBytes += sizeof(zeroActiveLeafCount);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);

    compactionInput.NodeCount = nodeCapacity;
    RunGpuRoamActiveLeafCompactionPass(compactionInput);

    GpuRoamMeshEmitPassInput emitInput{};
    emitInput.ProgramId = _state.MeshEmitProgramId;
    emitInput.NodeBufferId = _state.NodeBufferId;
    emitInput.ActiveLeafBufferId = _state.ActiveLeafBufferId;
    emitInput.CounterBufferId = _state.CounterBufferId;
    emitInput.VertexBufferId = _state.GpuVertexBufferId;
    emitInput.IndexBufferId = _state.GpuIndexBufferId;
    emitInput.IndirectDrawBufferId = _state.IndirectDrawBufferId;
    emitInput.HeightMapTextureId = _state.HeightMapTextureId;
    emitInput.ActiveLeafCapacity = activeLeafCapacity;
    emitInput.MaxDepth = input.Settings.MaxDepth;
    emitInput.BuildSequence = snapshot.BuildSequence;
    emitInput.TerrainSize = input.Settings.TerrainSize;
    emitInput.HeightScale = input.Settings.HeightScale;
    RunGpuRoamMeshEmitPass(emitInput);

    glEndQuery(GL_TIME_ELAPSED);

    GLuint64 elapsedNanoseconds = 0U;
    glGetQueryObjectui64v(_state.TimerQueryId, GL_QUERY_RESULT, &elapsedNanoseconds);
    gpuComputeMilliseconds = static_cast<float>(static_cast<double>(elapsedNanoseconds) / 1'000'000.0);

    GpuRoamCounters counters{};
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, _state.CounterBufferId);
    glGetBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, static_cast<GLsizeiptr>(sizeof(counters)), &counters);
    readbackBytes += sizeof(counters);

    const std::size_t sampleCount = std::min(activeLeafCount, GpuRoamDebugSampleCount);
    if (sampleCount > 0U)
    {
        std::array<std::uint32_t, GpuRoamDebugSampleCount> leafSamples{};
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, _state.ActiveLeafBufferId);
        glGetBufferSubData(
            GL_SHADER_STORAGE_BUFFER,
            0,
            static_cast<GLsizeiptr>(sampleCount * sizeof(std::uint32_t)),
            leafSamples.data());
        readbackBytes += sampleCount * sizeof(std::uint32_t);

        std::array<float, GpuRoamDebugSampleCount> errorSamples{};
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, _state.ScreenErrorBufferId);
        glGetBufferSubData(
            GL_SHADER_STORAGE_BUFFER,
            0,
            static_cast<GLsizeiptr>(sampleCount * sizeof(float)),
            errorSamples.data());
        readbackBytes += sampleCount * sizeof(float);
    }

    glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
    glBindTexture(GL_TEXTURE_2D, 0);

    gpuActiveLeafCount = counters.ActiveLeafCount;
    gpuNodeCount = std::min<std::size_t>(counters.AllocatedNodeCount, nodeCapacity);
    gpuSplitOnlyCommitCount = counters.SplitOnlyCommitCount;

    if (counters.ActiveLeafCount < activeLeafCount ||
        counters.ActiveLeafCount > activeLeafCapacity)
    {
        if (errorMessage != nullptr)
        {
            std::ostringstream stream;
            stream << "GPU ROAM-like active leaf compaction mismatch after split-only topology: gpu="
                   << counters.ActiveLeafCount << " expected range=[" << activeLeafCount
                   << ", " << activeLeafCapacity << "]";
            *errorMessage = stream.str();
        }
        return false;
    }

    return true;
}
} // namespace ParallelRoam::Algorithms::GpuRoam
