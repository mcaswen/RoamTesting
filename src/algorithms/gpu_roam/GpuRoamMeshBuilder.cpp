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

struct GpuRoamUploadMetrics
{
    float CpuUploadMilliseconds{0.0F};
    float BufferAllocationMilliseconds{0.0F};
};

float ElapsedMilliseconds(Clock::time_point start, Clock::time_point end)
{
    return std::chrono::duration<float, std::milli>(end - start).count();
}

std::string BuildGpuStatusMessage(bool usesIndirectDraw)
{
    if (usesIndirectDraw)
    {
        return "GPU ROAM-like: CPU DOD baseline, GPU split-only topology, mesh emit and indirect draw";
    }

    return "GPU ROAM-like: CPU DOD baseline, GPU split-only topology, mesh emit and GPU buffer draw";
}

bool EnsureBufferCapacity(
    GLenum target,
    std::uint32_t& bufferId,
    std::size_t& currentCapacityBytes,
    std::size_t requiredCapacityBytes,
    GpuRoamUploadMetrics& metrics,
    std::string* errorMessage)
{
    const std::size_t safeRequiredCapacityBytes = std::max<std::size_t>(requiredCapacityBytes, 1U);
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

    if (currentCapacityBytes < safeRequiredCapacityBytes)
    {
        const auto allocationStart = Clock::now();
        glBindBuffer(target, bufferId);
        glBufferData(target, static_cast<GLsizeiptr>(safeRequiredCapacityBytes), nullptr, GL_DYNAMIC_DRAW);
        glBindBuffer(target, 0);
        metrics.BufferAllocationMilliseconds += ElapsedMilliseconds(allocationStart, Clock::now());
        currentCapacityBytes = safeRequiredCapacityBytes;
    }
    return true;
}

bool UploadBufferRange(
    GLenum target,
    std::uint32_t& bufferId,
    std::size_t& currentCapacityBytes,
    const void* data,
    std::size_t dataByteCount,
    std::size_t capacityByteCount,
    GpuRoamUploadMetrics& metrics,
    std::string* errorMessage)
{
    if (dataByteCount > capacityByteCount)
    {
        if (errorMessage != nullptr)
        {
            *errorMessage = "GPU ROAM-like buffer allocation failed";
        }
        return false;
    }

    if (!EnsureBufferCapacity(target, bufferId, currentCapacityBytes, capacityByteCount, metrics, errorMessage))
    {
        return false;
    }

    if (data != nullptr && dataByteCount > 0U)
    {
        const auto uploadStart = Clock::now();
        glBindBuffer(target, bufferId);
        glBufferSubData(target, 0, static_cast<GLsizeiptr>(dataByteCount), data);
        glBindBuffer(target, 0);
        metrics.CpuUploadMilliseconds += ElapsedMilliseconds(uploadStart, Clock::now());
    }
    return true;
}

bool HeightMapTextureMatches(const GpuRoamState& state, const Terrain::HeightMap& heightMap)
{
    return state.HeightMapTextureUploaded &&
           state.HeightMapTextureId != 0U &&
           state.CachedHeightMapWidth == heightMap.Width() &&
           state.CachedHeightMapHeight == heightMap.Height() &&
           state.CachedHeightMapPath == heightMap.SourcePath();
}

bool UploadHeightMapTextureIfNeeded(
    const Terrain::HeightMap& heightMap,
    GpuRoamState& state,
    std::size_t& uploadBytes,
    GpuRoamUploadMetrics& metrics,
    std::string* errorMessage)
{
    if (HeightMapTextureMatches(state, heightMap))
    {
        return true;
    }

    if (state.HeightMapTextureId == 0U)
    {
        GLuint nextTextureId = 0U;
        glGenTextures(1, &nextTextureId);
        state.HeightMapTextureId = nextTextureId;
    }

    if (state.HeightMapTextureId == 0U)
    {
        if (errorMessage != nullptr)
        {
            *errorMessage = "GPU ROAM-like height map texture allocation failed";
        }
        return false;
    }

    const auto uploadStart = Clock::now();
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

    glBindTexture(GL_TEXTURE_2D, state.HeightMapTextureId);
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
    metrics.CpuUploadMilliseconds += ElapsedMilliseconds(uploadStart, Clock::now());
    state.CachedHeightMapPath = heightMap.SourcePath();
    state.CachedHeightMapWidth = heightMap.Width();
    state.CachedHeightMapHeight = heightMap.Height();
    state.HeightMapTextureUploaded = true;
    return true;
}

bool ResolveTimingReadbackSlot(
    GpuRoamState& state,
    std::size_t slotIndex,
    float& gpuComputeMilliseconds,
    std::size_t& readbackBytes,
    float& queryWaitMilliseconds,
    float& readbackWaitMilliseconds,
    std::string* errorMessage)
{
    GpuRoamTimingReadbackSlot& slot = state.TimingReadbackSlots[slotIndex];
    gpuComputeMilliseconds =
        state.HasCompletedTimingReadback ? state.LastCompletedGpuComputeMilliseconds : 0.0F;

    if (!slot.Pending)
    {
        return true;
    }

    GLuint queryAvailable = GL_FALSE;
    glGetQueryObjectuiv(slot.TimerQueryId, GL_QUERY_RESULT_AVAILABLE, &queryAvailable);

    const auto queryWaitStart = Clock::now();
    GLuint64 elapsedNanoseconds = 0U;
    // 延迟多个 slot 后通常已 ready；若仍未 ready，这里会等待并把等待时间单独记账
    glGetQueryObjectui64v(slot.TimerQueryId, GL_QUERY_RESULT, &elapsedNanoseconds);
    queryWaitMilliseconds += ElapsedMilliseconds(queryWaitStart, Clock::now());

    GpuRoamCounters counters{};
    const auto readbackStart = Clock::now();
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, slot.CounterBufferId);
    glGetBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, static_cast<GLsizeiptr>(sizeof(counters)), &counters);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
    readbackWaitMilliseconds += ElapsedMilliseconds(readbackStart, Clock::now());
    readbackBytes += sizeof(counters);

    const std::size_t expectedActiveLeafCount =
        slot.BaseActiveLeafCount + static_cast<std::size_t>(counters.SplitOnlyCommitCount);
    const std::size_t expectedAllocatedNodeCount =
        slot.BaseNodeCount + static_cast<std::size_t>(counters.SplitOnlyCommitCount) * 2U;
    if (counters.ActiveLeafCount != expectedActiveLeafCount ||
        counters.AllocatedNodeCount != expectedAllocatedNodeCount ||
        expectedActiveLeafCount > slot.ActiveLeafCapacity ||
        expectedAllocatedNodeCount > slot.NodeCapacity)
    {
        if (errorMessage != nullptr)
        {
            std::ostringstream stream;
            stream << "GPU ROAM-like delayed topology count mismatch: active leaves gpu="
                   << counters.ActiveLeafCount << " expected=" << expectedActiveLeafCount
                   << ", allocated nodes gpu=" << counters.AllocatedNodeCount
                   << " expected=" << expectedAllocatedNodeCount
                   << ", queryAvailable=" << (queryAvailable == GL_TRUE ? "true" : "false");
            *errorMessage = stream.str();
        }
        slot.Pending = false;
        return false;
    }

    state.LastCompletedCounters = counters;
    state.LastCompletedGpuComputeMilliseconds =
        static_cast<float>(static_cast<double>(elapsedNanoseconds) / 1'000'000.0);
    state.HasCompletedTimingReadback = true;
    gpuComputeMilliseconds = state.LastCompletedGpuComputeMilliseconds;
    slot.Pending = false;
    return true;
}

bool EnsureTimingReadbackSlot(
    GpuRoamState& state,
    std::size_t slotIndex,
    GpuRoamUploadMetrics& metrics,
    std::string* errorMessage)
{
    GpuRoamTimingReadbackSlot& slot = state.TimingReadbackSlots[slotIndex];
    if (slot.TimerQueryId == 0U)
    {
        GLuint queryId = 0U;
        glGenQueries(1, &queryId);
        slot.TimerQueryId = queryId;
    }

    if (slot.TimerQueryId == 0U)
    {
        if (errorMessage != nullptr)
        {
            *errorMessage = "GPU ROAM-like timer query allocation failed";
        }
        return false;
    }

    if (!EnsureBufferCapacity(
            GL_SHADER_STORAGE_BUFFER,
            slot.CounterBufferId,
            slot.CounterBufferCapacityBytes,
            sizeof(GpuRoamCounters),
            metrics,
            errorMessage))
    {
        return false;
    }

    state.CounterBufferId = slot.CounterBufferId;
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
    float bufferAllocationMilliseconds = 0.0F;
    if (!UploadSnapshot(
            snapshot,
            *input.HeightMap,
            gpuNodeCapacity,
            uploadBytes,
            cpuUploadMilliseconds,
            bufferAllocationMilliseconds,
            errorMessage))
    {
        return false;
    }

    float gpuComputeMilliseconds = 0.0F;
    std::size_t readbackBytes = 0U;
    float dispatchWallMilliseconds = 0.0F;
    float queryWaitMilliseconds = 0.0F;
    float readbackWaitMilliseconds = 0.0F;
    std::size_t gpuActiveLeafCount = snapshot.ActiveLeafIndices.size();
    std::size_t gpuNodeCount = snapshot.Nodes.size();
    std::size_t gpuSplitOnlyCommitCount = 0U;
    if (!RunGpuComputePipeline(
            snapshot,
            input,
            uploadBytes,
            gpuComputeMilliseconds,
            readbackBytes,
            bufferAllocationMilliseconds,
            dispatchWallMilliseconds,
            queryWaitMilliseconds,
            readbackWaitMilliseconds,
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
    inOutStats.GpuBufferAllocationMilliseconds = bufferAllocationMilliseconds;
    inOutStats.GpuDispatchWallMilliseconds = dispatchWallMilliseconds;
    inOutStats.GpuQueryWaitMilliseconds = queryWaitMilliseconds;
    inOutStats.GpuReadbackWaitMilliseconds = readbackWaitMilliseconds;
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
    outPacket.GpuResourceLifetime = TerrainLodGpuResourceLifetime::UntilNextBuildOrReset;
    outPacket.GpuResourceGeneration = snapshot.BuildSequence;
    outPacket.ActiveLeafCount = gpuActiveLeafCount;
    outPacket.ActiveTriangleCount = inOutStats.ActiveTriangleCount;
    outPacket.IndexCount = gpuActiveLeafCount * 3U;
    return outPacket.ActiveTriangleCount > 0U &&
           outPacket.IndexCount > 0U &&
           outPacket.GpuVertexBufferId != 0U &&
           outPacket.GpuIndexBufferId != 0U &&
           (outPacket.Mode != TerrainLodRenderMode::GpuIndirect || outPacket.IndirectDrawBufferId != 0U) &&
           outPacket.HasConsistentResourceContract();
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
    float& bufferAllocationMilliseconds,
    std::string* errorMessage)
{
    GpuRoamUploadMetrics metrics{};
    if (!UploadBufferRange(
            GL_SHADER_STORAGE_BUFFER,
            _state.NodeBufferId,
            _state.NodeBufferCapacityBytes,
            snapshot.Nodes.data(),
            snapshot.NodeBufferBytes(),
            nodeCapacity * sizeof(GpuRoamNodeRecord),
            metrics,
            errorMessage))
    {
        return false;
    }
    uploadBytes += snapshot.NodeBufferBytes();

    if (!EnsureBufferCapacity(
            GL_SHADER_STORAGE_BUFFER,
            _state.ActiveLeafBufferId,
            _state.ActiveLeafBufferCapacityBytes,
            nodeCapacity * sizeof(std::uint32_t),
            metrics,
            errorMessage))
    {
        return false;
    }

    if (!UploadHeightMapTextureIfNeeded(heightMap, _state, uploadBytes, metrics, errorMessage))
    {
        return false;
    }
    cpuUploadMilliseconds = metrics.CpuUploadMilliseconds;
    bufferAllocationMilliseconds = metrics.BufferAllocationMilliseconds;
    return true;
}

bool GpuRoamMeshBuilder::RunGpuComputePipeline(
    const GpuRoamBufferSnapshot& snapshot,
    const TerrainLodBuildInput& input,
    std::size_t& uploadBytes,
    float& gpuComputeMilliseconds,
    std::size_t& readbackBytes,
    float& bufferAllocationMilliseconds,
    float& dispatchWallMilliseconds,
    float& queryWaitMilliseconds,
    float& readbackWaitMilliseconds,
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

    GpuRoamUploadMetrics metrics{};
    const std::size_t timingSlotIndex = _state.TimingReadbackCursor % GpuRoamTimingReadbackSlotCount;
    if (!ResolveTimingReadbackSlot(
            _state,
            timingSlotIndex,
            gpuComputeMilliseconds,
            readbackBytes,
            queryWaitMilliseconds,
            readbackWaitMilliseconds,
            errorMessage) ||
        !EnsureTimingReadbackSlot(_state, timingSlotIndex, metrics, errorMessage))
    {
        return false;
    }

    GpuRoamCounters zeroCounters{};
    zeroCounters.AllocatedNodeCount = static_cast<std::uint32_t>(nodeCount);
    if (!EnsureBufferCapacity(
            GL_SHADER_STORAGE_BUFFER,
            _state.ScreenErrorBufferId,
            _state.ScreenErrorBufferCapacityBytes,
            screenErrorBytes,
            metrics,
            errorMessage) ||
        !EnsureBufferCapacity(
            GL_SHADER_STORAGE_BUFFER,
            _state.SplitCandidateBufferId,
            _state.SplitCandidateBufferCapacityBytes,
            candidateBufferBytes,
            metrics,
            errorMessage) ||
        !EnsureBufferCapacity(
            GL_SHADER_STORAGE_BUFFER,
            _state.MergeCandidateBufferId,
            _state.MergeCandidateBufferCapacityBytes,
            candidateBufferBytes,
            metrics,
            errorMessage) ||
        !EnsureBufferCapacity(
            GL_SHADER_STORAGE_BUFFER,
            _state.GpuVertexBufferId,
            _state.GpuVertexBufferCapacityBytes,
            vertexBufferBytes,
            metrics,
            errorMessage) ||
        !EnsureBufferCapacity(
            GL_SHADER_STORAGE_BUFFER,
            _state.GpuIndexBufferId,
            _state.GpuIndexBufferCapacityBytes,
            indexBufferBytes,
            metrics,
            errorMessage) ||
        !UploadBufferRange(
            GL_SHADER_STORAGE_BUFFER,
            _state.IndirectDrawBufferId,
            _state.IndirectDrawBufferCapacityBytes,
            &emptyIndirectCommand,
            sizeof(emptyIndirectCommand),
            sizeof(emptyIndirectCommand),
            metrics,
            errorMessage) ||
        !UploadBufferRange(
            GL_SHADER_STORAGE_BUFFER,
            _state.CounterBufferId,
            _state.TimingReadbackSlots[timingSlotIndex].CounterBufferCapacityBytes,
            &zeroCounters,
            sizeof(zeroCounters),
            sizeof(zeroCounters),
            metrics,
            errorMessage))
    {
        return false;
    }
    uploadBytes += sizeof(zeroCounters);
    uploadBytes += sizeof(emptyIndirectCommand);
    bufferAllocationMilliseconds += metrics.BufferAllocationMilliseconds;

    const auto dispatchWallStart = Clock::now();
    glBeginQuery(GL_TIME_ELAPSED, _state.TimingReadbackSlots[timingSlotIndex].TimerQueryId);

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
    emitInput.NodeCapacity = nodeCapacity;
    emitInput.MaxDepth = input.Settings.MaxDepth;
    emitInput.BuildSequence = snapshot.BuildSequence;
    emitInput.TerrainSize = input.Settings.TerrainSize;
    emitInput.HeightScale = input.Settings.HeightScale;
    RunGpuRoamMeshEmitPass(emitInput);

    glEndQuery(GL_TIME_ELAPSED);
    dispatchWallMilliseconds += ElapsedMilliseconds(dispatchWallStart, Clock::now());

    glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
    glBindTexture(GL_TEXTURE_2D, 0);

    GpuRoamTimingReadbackSlot& slot = _state.TimingReadbackSlots[timingSlotIndex];
    slot.BaseActiveLeafCount = activeLeafCount;
    slot.BaseNodeCount = nodeCount;
    slot.ActiveLeafCapacity = activeLeafCapacity;
    slot.NodeCapacity = nodeCapacity;
    slot.Pending = true;
    _state.TimingReadbackCursor = (_state.TimingReadbackCursor + 1U) % GpuRoamTimingReadbackSlotCount;

    // 当前帧不再同步等待 GPU counter。Renderer 对 indirect draw 使用 GPU 端 command，
    // CPU 统计保留 DOD baseline，延迟读回只用于后续 timing 和防御验证。
    gpuActiveLeafCount = activeLeafCount;
    gpuNodeCount = nodeCount;
    gpuSplitOnlyCommitCount = 0U;

    return true;
}
} // namespace ParallelRoam::Algorithms::GpuRoam
