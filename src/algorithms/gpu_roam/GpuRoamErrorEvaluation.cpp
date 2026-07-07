#include "algorithms/gpu_roam/GpuRoamErrorEvaluation.h"

#include "algorithms/gpu_roam/GpuRoamComputeSupport.h"

#include <glad/gl.h>

namespace ParallelRoam::Algorithms::GpuRoam
{
namespace
{
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

const float minimumDistanceScale = 0.01;
const float projectedEdgeWeight = 0.20;

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

float distanceWeight(float distanceToCamera)
{
    float safeDistanceScale = max(uDistanceScale, minimumDistanceScale);
    float normalizedDistance = safeDistanceScale / distanceToCamera;
    return normalizedDistance * normalizedDistance;
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
    float distanceScale = max(uDistanceScale, minimumDistanceScale);
    float weight = distanceWeight(distanceToCamera);
    float heightErrorScore = worldError * weight;
    float edgeLengthScore = longestEdgeLength * projectedEdgeWeight / distanceScale * weight;
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
} // namespace

bool EnsureGpuRoamErrorEvaluationProgram(
    std::uint32_t& programId,
    std::string* errorMessage)
{
    return EnsureGpuRoamComputeProgram(
        programId,
        ErrorEvaluationComputeSource,
        "error evaluation",
        errorMessage);
}

void RunGpuRoamErrorEvaluationPass(const GpuRoamErrorEvaluationPassInput& input)
{
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, input.NodeBufferId);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, input.ActiveLeafBufferId);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 2, input.ScreenErrorBufferId);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, input.HeightMapTextureId);

    glUseProgram(input.ProgramId);
    SetGpuRoamProgramInt(input.ProgramId, "uHeightMap", 0);
    SetGpuRoamProgramUInt(input.ProgramId, "uActiveLeafCount", static_cast<std::uint32_t>(input.ActiveLeafCount));
    SetGpuRoamProgramFloat(input.ProgramId, "uTerrainSize", input.TerrainSize);
    SetGpuRoamProgramFloat(input.ProgramId, "uHeightScale", input.HeightScale);
    SetGpuRoamProgramFloat(input.ProgramId, "uDistanceScale", input.DistanceScale);
    SetGpuRoamProgramVec3(input.ProgramId, "uCameraPosition", input.CameraPosition);
    glDispatchCompute(GpuRoamWorkGroupCount(input.ActiveLeafCount), 1U, 1U);
    glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
}
} // namespace ParallelRoam::Algorithms::GpuRoam
