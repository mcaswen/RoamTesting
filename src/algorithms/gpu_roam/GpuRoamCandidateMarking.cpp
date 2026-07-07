#include "algorithms/gpu_roam/GpuRoamCandidateMarking.h"

#include "algorithms/gpu_roam/GpuRoamComputeSupport.h"

#include <glad/gl.h>

#include <algorithm>

namespace ParallelRoam::Algorithms::GpuRoam
{
namespace
{
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

const float projectedEdgeWeight = 0.20;
const float defaultDistanceScale = 24.0;
const float nearDistanceRadiusMultiplier = 2.0;

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

float nearDistanceBoost(float distanceToCamera)
{
    float safeDistanceScale = max(uDistanceScale, 0.0);
    float nearDistanceWeight = safeDistanceScale * nearDistanceRadiusMultiplier / distanceToCamera;
    return max(1.0, sqrt(max(nearDistanceWeight, 0.0)));
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
    float distanceScale = max(uDistanceScale, 0.0);
    float nearBoost = nearDistanceBoost(distanceToCamera);
    float heightErrorScore = worldError * distanceScale / distanceToCamera * nearBoost;
    float edgeLengthScore =
        longestEdgeLength * projectedEdgeWeight * (distanceScale / defaultDistanceScale) / distanceToCamera * nearBoost;
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
} // namespace

bool EnsureGpuRoamCandidateMarkingProgram(
    std::uint32_t& programId,
    std::string* errorMessage)
{
    return EnsureGpuRoamComputeProgram(
        programId,
        CandidateMarkingComputeSource,
        "candidate marking",
        errorMessage);
}

void RunGpuRoamCandidateMarkingPass(const GpuRoamCandidateMarkingPassInput& input)
{
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, input.NodeBufferId);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, input.ActiveLeafBufferId);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 2, input.ScreenErrorBufferId);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 3, input.CounterBufferId);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 4, input.SplitCandidateBufferId);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 5, input.MergeCandidateBufferId);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, input.HeightMapTextureId);

    glUseProgram(input.ProgramId);
    SetGpuRoamProgramInt(input.ProgramId, "uHeightMap", 0);
    SetGpuRoamProgramUInt(input.ProgramId, "uNodeCount", static_cast<std::uint32_t>(input.NodeCount));
    SetGpuRoamProgramUInt(input.ProgramId, "uActiveLeafLimit", static_cast<std::uint32_t>(input.ActiveLeafLimit));
    SetGpuRoamProgramUInt(input.ProgramId, "uMaxDepth", static_cast<std::uint32_t>(std::max(input.MaxDepth, 0)));
    SetGpuRoamProgramFloat(input.ProgramId, "uTerrainSize", input.TerrainSize);
    SetGpuRoamProgramFloat(input.ProgramId, "uHeightScale", input.HeightScale);
    SetGpuRoamProgramFloat(input.ProgramId, "uDistanceScale", input.DistanceScale);
    SetGpuRoamProgramFloat(input.ProgramId, "uSplitThreshold", input.SplitThreshold);
    SetGpuRoamProgramFloat(input.ProgramId, "uMergeThreshold", std::min(input.MergeThreshold, input.SplitThreshold));
    SetGpuRoamProgramVec3(input.ProgramId, "uCameraPosition", input.CameraPosition);
    glDispatchCompute(GpuRoamWorkGroupCount(std::max(input.NodeCount, input.ActiveLeafLimit)), 1U, 1U);
    glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
}
} // namespace ParallelRoam::Algorithms::GpuRoam
