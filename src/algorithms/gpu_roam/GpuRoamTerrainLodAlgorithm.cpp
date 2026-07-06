#include "algorithms/gpu_roam/GpuRoamTerrainLodAlgorithm.h"

#include "algorithms/TerrainLodProfiling.h"
#include "algorithms/gpu_roam/GpuRoamBufferSchema.h"
#include "platform/OpenGlCapabilities.h"

#include <glad/gl.h>
#include <glm/glm.hpp>

#include <array>
#include <algorithm>
#include <chrono>
#include <cstddef>
#include <sstream>
#include <string_view>
#include <vector>

namespace ParallelRoam::Algorithms::GpuRoam
{
namespace
{
using Clock = std::chrono::steady_clock;
constexpr GLuint InvalidProgramId = 0U;
constexpr GLuint LocalWorkGroupSize = 128U;
constexpr std::size_t GpuRoamDebugSampleCount = 8U;

constexpr const char* ActiveLeafCompactionComputeSource = R"(
#version 430 core
layout(local_size_x = 128) in;

struct NodeRecord
{
    vec4 domainAAndB;
    vec4 domainCAndErrors;
    uvec4 topology0;
    uvec4 topology1;
    uvec4 pathAndCreatedBuild;
    uvec4 activatedAndSplitBuild;
    uvec4 mergeBuildAndDepth;
};

layout(std430, binding = 0) readonly buffer NodeBuffer
{
    NodeRecord nodes[];
};

layout(std430, binding = 1) writeonly buffer ActiveLeafBuffer
{
    uint activeLeafIndices[];
};

layout(std430, binding = 3) buffer CounterBuffer
{
    uint activeLeafCount;
    uint splitCandidateCount;
    uint mergeCandidateCount;
    uint reservedCounter;
};

uniform uint uNodeCount;

void main()
{
    uint nodeIndex = gl_GlobalInvocationID.x;
    if (nodeIndex >= uNodeCount)
    {
        return;
    }

    const uint activeLeafFlag = 1u << 2u;
    uint flags = nodes[nodeIndex].topology1.w;
    if ((flags & activeLeafFlag) == 0u)
    {
        return;
    }

    uint outputIndex = atomicAdd(activeLeafCount, 1u);
    activeLeafIndices[outputIndex] = nodeIndex;
}
)";

constexpr const char* ErrorEvaluationComputeSource = R"(
#version 430 core
layout(local_size_x = 128) in;

struct NodeRecord
{
    vec4 domainAAndB;
    vec4 domainCAndErrors;
    uvec4 topology0;
    uvec4 topology1;
    uvec4 pathAndCreatedBuild;
    uvec4 activatedAndSplitBuild;
    uvec4 mergeBuildAndDepth;
};

layout(std430, binding = 0) readonly buffer NodeBuffer
{
    NodeRecord nodes[];
};

layout(std430, binding = 1) readonly buffer ActiveLeafBuffer
{
    uint activeLeafIndices[];
};

layout(std430, binding = 2) writeonly buffer ScreenErrorBuffer
{
    float screenErrors[];
};

layout(binding = 0) uniform sampler2D uHeightMap;

uniform uint uActiveLeafCount;
uniform float uTerrainSize;
uniform float uHeightScale;
uniform float uDistanceScale;
uniform vec3 uCameraPosition;

float sampleHeight(vec2 uv)
{
    return texture(uHeightMap, clamp(uv, vec2(0.0), vec2(1.0))).r;
}

vec3 domainToWorld(vec2 uv)
{
    return vec3(
        (uv.x - 0.5) * uTerrainSize,
        sampleHeight(uv) * uHeightScale,
        (uv.y - 0.5) * uTerrainSize);
}

float scoreNode(uint nodeIndex)
{
    NodeRecord node = nodes[nodeIndex];
    vec2 aUv = node.domainAAndB.xy;
    vec2 bUv = node.domainAAndB.zw;
    vec2 cUv = node.domainCAndErrors.xy;

    vec3 a = domainToWorld(aUv);
    vec3 b = domainToWorld(bUv);
    vec3 c = domainToWorld(cUv);
    vec3 center = (a + b + c) / 3.0;
    float distanceToCamera = max(length(center - uCameraPosition), 0.05);
    float worldError = node.domainCAndErrors.z * uHeightScale;
    float longestEdgeLength = max(max(length(a - b), length(b - c)), length(c - a));
    float heightErrorScore = worldError * uDistanceScale / distanceToCamera;
    float edgeLengthScore = longestEdgeLength * 0.20 / distanceToCamera;
    return max(heightErrorScore, edgeLengthScore);
}

void main()
{
    uint leafSlot = gl_GlobalInvocationID.x;
    if (leafSlot >= uActiveLeafCount)
    {
        return;
    }

    uint nodeIndex = activeLeafIndices[leafSlot];
    screenErrors[leafSlot] = scoreNode(nodeIndex);
}
)";

constexpr const char* CandidateMarkingComputeSource = R"(
#version 430 core
layout(local_size_x = 128) in;

struct NodeRecord
{
    vec4 domainAAndB;
    vec4 domainCAndErrors;
    uvec4 topology0;
    uvec4 topology1;
    uvec4 pathAndCreatedBuild;
    uvec4 activatedAndSplitBuild;
    uvec4 mergeBuildAndDepth;
};

layout(std430, binding = 0) readonly buffer NodeBuffer
{
    NodeRecord nodes[];
};

layout(std430, binding = 1) readonly buffer ActiveLeafBuffer
{
    uint activeLeafIndices[];
};

layout(std430, binding = 2) readonly buffer ScreenErrorBuffer
{
    float screenErrors[];
};

layout(std430, binding = 3) buffer CounterBuffer
{
    uint activeLeafCount;
    uint splitCandidateCount;
    uint mergeCandidateCount;
    uint reservedCounter;
};

layout(std430, binding = 4) writeonly buffer SplitCandidateBuffer
{
    uint splitCandidates[];
};

layout(std430, binding = 5) writeonly buffer MergeCandidateBuffer
{
    uint mergeCandidates[];
};

layout(binding = 0) uniform sampler2D uHeightMap;

uniform uint uNodeCount;
uniform uint uActiveLeafLimit;
uniform uint uMaxDepth;
uniform float uTerrainSize;
uniform float uHeightScale;
uniform float uDistanceScale;
uniform float uSplitThreshold;
uniform float uMergeThreshold;
uniform vec3 uCameraPosition;

float sampleHeight(vec2 uv)
{
    return texture(uHeightMap, clamp(uv, vec2(0.0), vec2(1.0))).r;
}

vec3 domainToWorld(vec2 uv)
{
    return vec3(
        (uv.x - 0.5) * uTerrainSize,
        sampleHeight(uv) * uHeightScale,
        (uv.y - 0.5) * uTerrainSize);
}

float scoreNode(uint nodeIndex)
{
    NodeRecord node = nodes[nodeIndex];
    vec2 aUv = node.domainAAndB.xy;
    vec2 bUv = node.domainAAndB.zw;
    vec2 cUv = node.domainCAndErrors.xy;

    vec3 a = domainToWorld(aUv);
    vec3 b = domainToWorld(bUv);
    vec3 c = domainToWorld(cUv);
    vec3 center = (a + b + c) / 3.0;
    float distanceToCamera = max(length(center - uCameraPosition), 0.05);
    float worldError = node.domainCAndErrors.z * uHeightScale;
    float longestEdgeLength = max(max(length(a - b), length(b - c)), length(c - a));
    float heightErrorScore = worldError * uDistanceScale / distanceToCamera;
    float edgeLengthScore = longestEdgeLength * 0.20 / distanceToCamera;
    return max(heightErrorScore, edgeLengthScore);
}

void main()
{
    uint index = gl_GlobalInvocationID.x;

    if (index < uActiveLeafLimit)
    {
        uint nodeIndex = activeLeafIndices[index];
        uint depth = nodes[nodeIndex].mergeBuildAndDepth.z;
        float screenError = screenErrors[index];
        if (depth < uMaxDepth && screenError >= uSplitThreshold)
        {
            uint outputIndex = atomicAdd(splitCandidateCount, 1u);
            splitCandidates[outputIndex] = nodeIndex;
        }
    }

    if (index >= uNodeCount)
    {
        return;
    }

    const uint splitFlag = 1u << 0u;
    uint flags = nodes[index].topology1.w;
    if ((flags & splitFlag) == 0u)
    {
        return;
    }

    float mergeScore = scoreNode(index);
    if (mergeScore <= uMergeThreshold)
    {
        uint outputIndex = atomicAdd(mergeCandidateCount, 1u);
        mergeCandidates[outputIndex] = index;
    }
}
)";

struct GpuRoamCounters
{
    std::uint32_t ActiveLeafCount{0};
    std::uint32_t SplitCandidateCount{0};
    std::uint32_t MergeCandidateCount{0};
    std::uint32_t Reserved{0};
};

std::string BuildStagingStatusMessage()
{
    return "GPU ROAM-like 4C-4E staging: CPU DOD topology and CPU mesh fallback are active, GPU compaction/error/candidate shadow passes are running";
}

std::string ReadShaderLog(GLuint shaderId)
{
    GLint logLength = 0;
    glGetShaderiv(shaderId, GL_INFO_LOG_LENGTH, &logLength);
    if (logLength <= 1)
    {
        return {};
    }

    std::string log(static_cast<std::size_t>(logLength), '\0');
    GLsizei written = 0;
    glGetShaderInfoLog(shaderId, logLength, &written, log.data());
    log.resize(static_cast<std::size_t>(std::max(written, 0)));
    return log;
}

std::string ReadProgramLog(GLuint programId)
{
    GLint logLength = 0;
    glGetProgramiv(programId, GL_INFO_LOG_LENGTH, &logLength);
    if (logLength <= 1)
    {
        return {};
    }

    std::string log(static_cast<std::size_t>(logLength), '\0');
    GLsizei written = 0;
    glGetProgramInfoLog(programId, logLength, &written, log.data());
    log.resize(static_cast<std::size_t>(std::max(written, 0)));
    return log;
}

bool EnsureComputeProgram(
    std::uint32_t& programId,
    const char* source,
    std::string_view label,
    std::string* errorMessage)
{
    if (programId != InvalidProgramId)
    {
        return true;
    }

    const GLuint shaderId = glCreateShader(GL_COMPUTE_SHADER);
    const GLchar* shaderSource = source;
    glShaderSource(shaderId, 1, &shaderSource, nullptr);
    glCompileShader(shaderId);

    GLint shaderCompiled = GL_FALSE;
    glGetShaderiv(shaderId, GL_COMPILE_STATUS, &shaderCompiled);
    if (shaderCompiled != GL_TRUE)
    {
        if (errorMessage != nullptr)
        {
            *errorMessage = "GPU ROAM-like compute shader compile failed (" +
                            std::string{label} + "):\n" + ReadShaderLog(shaderId);
        }
        glDeleteShader(shaderId);
        return false;
    }

    const GLuint nextProgramId = glCreateProgram();
    glAttachShader(nextProgramId, shaderId);
    glLinkProgram(nextProgramId);
    glDeleteShader(shaderId);

    GLint programLinked = GL_FALSE;
    glGetProgramiv(nextProgramId, GL_LINK_STATUS, &programLinked);
    if (programLinked != GL_TRUE)
    {
        if (errorMessage != nullptr)
        {
            *errorMessage = "GPU ROAM-like compute program link failed (" +
                            std::string{label} + "):\n" + ReadProgramLog(nextProgramId);
        }
        glDeleteProgram(nextProgramId);
        return false;
    }

    programId = nextProgramId;
    return true;
}

GLuint WorkGroupCount(std::size_t itemCount)
{
    if (itemCount == 0U)
    {
        return 1U;
    }

    return static_cast<GLuint>((itemCount + LocalWorkGroupSize - 1U) / LocalWorkGroupSize);
}

void SetProgramUInt(GLuint programId, const char* name, std::uint32_t value)
{
    const GLint location = glGetUniformLocation(programId, name);
    if (location >= 0)
    {
        glUniform1ui(location, value);
    }
}

void SetProgramFloat(GLuint programId, const char* name, float value)
{
    const GLint location = glGetUniformLocation(programId, name);
    if (location >= 0)
    {
        glUniform1f(location, value);
    }
}

void SetProgramVec3(GLuint programId, const char* name, const glm::vec3& value)
{
    const GLint location = glGetUniformLocation(programId, name);
    if (location >= 0)
    {
        glUniform3f(location, value.x, value.y, value.z);
    }
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
            nullptr,
            snapshot.Nodes.size() * sizeof(std::uint32_t),
            errorMessage))
    {
        return false;
    }

    if (!UploadHeightMapTexture(*input.HeightMap, _heightMapTextureId, uploadBytes, errorMessage))
    {
        return false;
    }
    const auto uploadEnd = Clock::now();

    float gpuComputeMilliseconds = 0.0F;
    std::size_t readbackBytes = 0U;
    if (!RunGpuShadowPasses(snapshot, input, uploadBytes, gpuComputeMilliseconds, readbackBytes, errorMessage))
    {
        return false;
    }
    const TerrainLodCpuSample cpuSampleEnd = CaptureTerrainLodCpuSample();

    _stats = ToTerrainLodStats(_cpuTopologyBuilder.Stats());
    _stats.CpuGpuUploadBytes = uploadBytes;
    _stats.CpuGpuReadbackBytes = readbackBytes;
    _stats.GpuComputeMilliseconds = gpuComputeMilliseconds;
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

bool GpuRoamTerrainLodAlgorithm::RunGpuShadowPasses(
    const GpuRoamBufferSnapshot& snapshot,
    const TerrainLodBuildInput& input,
    std::size_t& uploadBytes,
    float& gpuComputeMilliseconds,
    std::size_t& readbackBytes,
    std::string* errorMessage)
{
    if (!EnsureComputeProgram(
            _activeLeafCompactionProgramId,
            ActiveLeafCompactionComputeSource,
            "active leaf compaction",
            errorMessage) ||
        !EnsureComputeProgram(
            _errorEvaluationProgramId,
            ErrorEvaluationComputeSource,
            "error evaluation",
            errorMessage) ||
        !EnsureComputeProgram(
            _candidateMarkingProgramId,
            CandidateMarkingComputeSource,
            "candidate marking",
            errorMessage))
    {
        return false;
    }

    const std::size_t nodeCount = snapshot.Nodes.size();
    const std::size_t activeLeafCount = snapshot.ActiveLeafIndices.size();
    const std::size_t screenErrorBytes = activeLeafCount * sizeof(float);
    const std::size_t candidateBufferBytes = std::max<std::size_t>(nodeCount, 1U) * sizeof(std::uint32_t);

    if (!UploadBuffer(GL_SHADER_STORAGE_BUFFER, _screenErrorBufferId, nullptr, screenErrorBytes, errorMessage) ||
        !UploadBuffer(GL_SHADER_STORAGE_BUFFER, _splitCandidateBufferId, nullptr, candidateBufferBytes, errorMessage) ||
        !UploadBuffer(GL_SHADER_STORAGE_BUFFER, _mergeCandidateBufferId, nullptr, candidateBufferBytes, errorMessage))
    {
        return false;
    }

    const GpuRoamCounters zeroCounters{};
    if (!UploadBuffer(GL_SHADER_STORAGE_BUFFER, _counterBufferId, &zeroCounters, sizeof(zeroCounters), errorMessage))
    {
        return false;
    }
    uploadBytes += sizeof(zeroCounters);

    if (_timerQueryId == 0U)
    {
        GLuint queryId = 0U;
        glGenQueries(1, &queryId);
        _timerQueryId = queryId;
    }

    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, _nodeBufferId);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, _activeLeafBufferId);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 2, _screenErrorBufferId);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 3, _counterBufferId);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 4, _splitCandidateBufferId);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 5, _mergeCandidateBufferId);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, _heightMapTextureId);

    glBeginQuery(GL_TIME_ELAPSED, _timerQueryId);

    glUseProgram(_activeLeafCompactionProgramId);
    SetProgramUInt(_activeLeafCompactionProgramId, "uNodeCount", static_cast<std::uint32_t>(nodeCount));
    glDispatchCompute(WorkGroupCount(nodeCount), 1U, 1U);
    glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);

    glUseProgram(_errorEvaluationProgramId);
    SetProgramUInt(_errorEvaluationProgramId, "uActiveLeafCount", static_cast<std::uint32_t>(activeLeafCount));
    SetProgramFloat(_errorEvaluationProgramId, "uTerrainSize", input.Settings.TerrainSize);
    SetProgramFloat(_errorEvaluationProgramId, "uHeightScale", input.Settings.HeightScale);
    SetProgramFloat(_errorEvaluationProgramId, "uDistanceScale", input.Settings.DistanceScale);
    SetProgramVec3(_errorEvaluationProgramId, "uCameraPosition", input.CameraPosition);
    glDispatchCompute(WorkGroupCount(activeLeafCount), 1U, 1U);
    glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);

    glUseProgram(_candidateMarkingProgramId);
    SetProgramUInt(_candidateMarkingProgramId, "uNodeCount", static_cast<std::uint32_t>(nodeCount));
    SetProgramUInt(_candidateMarkingProgramId, "uActiveLeafLimit", static_cast<std::uint32_t>(activeLeafCount));
    SetProgramUInt(_candidateMarkingProgramId, "uMaxDepth", static_cast<std::uint32_t>(std::max(input.Settings.MaxDepth, 0)));
    SetProgramFloat(_candidateMarkingProgramId, "uTerrainSize", input.Settings.TerrainSize);
    SetProgramFloat(_candidateMarkingProgramId, "uHeightScale", input.Settings.HeightScale);
    SetProgramFloat(_candidateMarkingProgramId, "uDistanceScale", input.Settings.DistanceScale);
    SetProgramFloat(_candidateMarkingProgramId, "uSplitThreshold", input.Settings.SplitThreshold);
    SetProgramFloat(_candidateMarkingProgramId, "uMergeThreshold", std::min(input.Settings.MergeThreshold, input.Settings.SplitThreshold));
    SetProgramVec3(_candidateMarkingProgramId, "uCameraPosition", input.CameraPosition);
    glDispatchCompute(WorkGroupCount(std::max(nodeCount, activeLeafCount)), 1U, 1U);
    glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT | GL_BUFFER_UPDATE_BARRIER_BIT);

    glEndQuery(GL_TIME_ELAPSED);

    GLuint64 elapsedNanoseconds = 0U;
    glGetQueryObjectui64v(_timerQueryId, GL_QUERY_RESULT, &elapsedNanoseconds);
    gpuComputeMilliseconds = static_cast<float>(static_cast<double>(elapsedNanoseconds) / 1'000'000.0);

    GpuRoamCounters counters{};
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, _counterBufferId);
    glGetBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, static_cast<GLsizeiptr>(sizeof(counters)), &counters);
    readbackBytes += sizeof(counters);

    const std::size_t sampleCount = std::min(activeLeafCount, GpuRoamDebugSampleCount);
    if (sampleCount > 0U)
    {
        std::array<std::uint32_t, GpuRoamDebugSampleCount> leafSamples{};
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, _activeLeafBufferId);
        glGetBufferSubData(
            GL_SHADER_STORAGE_BUFFER,
            0,
            static_cast<GLsizeiptr>(sampleCount * sizeof(std::uint32_t)),
            leafSamples.data());
        readbackBytes += sampleCount * sizeof(std::uint32_t);

        std::array<float, GpuRoamDebugSampleCount> errorSamples{};
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, _screenErrorBufferId);
        glGetBufferSubData(
            GL_SHADER_STORAGE_BUFFER,
            0,
            static_cast<GLsizeiptr>(sampleCount * sizeof(float)),
            errorSamples.data());
        readbackBytes += sampleCount * sizeof(float);
    }

    glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
    glBindTexture(GL_TEXTURE_2D, 0);

    if (counters.ActiveLeafCount != activeLeafCount)
    {
        if (errorMessage != nullptr)
        {
            std::ostringstream stream;
            stream << "GPU ROAM-like active leaf compaction mismatch: gpu="
                   << counters.ActiveLeafCount << " cpu=" << activeLeafCount;
            *errorMessage = stream.str();
        }
        return false;
    }

    return true;
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

    const std::array<std::uint32_t*, 4U> bufferIds{
        &_screenErrorBufferId,
        &_counterBufferId,
        &_splitCandidateBufferId,
        &_mergeCandidateBufferId,
    };
    if (glad_glDeleteBuffers != nullptr)
    {
        for (std::uint32_t* bufferId : bufferIds)
        {
            if (*bufferId != 0U)
            {
                const GLuint glBufferId = *bufferId;
                glDeleteBuffers(1, &glBufferId);
                *bufferId = 0U;
            }
        }
    }

    const std::array<std::uint32_t*, 3U> programIds{
        &_activeLeafCompactionProgramId,
        &_errorEvaluationProgramId,
        &_candidateMarkingProgramId,
    };
    if (glad_glDeleteProgram != nullptr)
    {
        for (std::uint32_t* programId : programIds)
        {
            if (*programId != 0U)
            {
                glDeleteProgram(*programId);
                *programId = 0U;
            }
        }
    }

    if (_timerQueryId != 0U && glad_glDeleteQueries != nullptr)
    {
        const GLuint queryId = _timerQueryId;
        glDeleteQueries(1, &queryId);
        _timerQueryId = 0U;
    }
}
} // namespace ParallelRoam::Algorithms::GpuRoam
