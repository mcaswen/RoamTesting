#include "algorithms/gpu_roam/GpuRoamActiveLeafCompaction.h"

#include "algorithms/gpu_roam/GpuRoamComputeSupport.h"

#include <glad/gl.h>

namespace ParallelRoam::Algorithms::GpuRoam
{
namespace
{
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
} // namespace

bool EnsureGpuRoamActiveLeafCompactionProgram(
    std::uint32_t& programId,
    std::string* errorMessage)
{
    return EnsureGpuRoamComputeProgram(
        programId,
        ActiveLeafCompactionComputeSource,
        "active leaf compaction",
        errorMessage);
}

void RunGpuRoamActiveLeafCompactionPass(const GpuRoamActiveLeafCompactionPassInput& input)
{
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, input.NodeBufferId);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, input.ActiveLeafBufferId);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 3, input.CounterBufferId);

    glUseProgram(input.ProgramId);
    SetGpuRoamProgramUInt(input.ProgramId, "uNodeCount", static_cast<std::uint32_t>(input.NodeCount));
    glDispatchCompute(GpuRoamWorkGroupCount(input.NodeCount), 1U, 1U);
    glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
}
} // namespace ParallelRoam::Algorithms::GpuRoam
