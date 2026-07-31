#include "algorithms/gpu_roam/GpuRoamActiveLeafReset.h"

#include "algorithms/gpu_roam/GpuRoamComputeSupport.h"

#include <glad/gl.h>

namespace ParallelRoam::Algorithms::GpuRoam
{
namespace
{
constexpr const char* ActiveLeafResetComputeSource = R"(
#version 430 core
layout(local_size_x = 1) in;

layout(std430, binding = 3) buffer CounterBuffer
{
    uint activeLeafCount;
    uint splitCandidateCount;
    uint mergeCandidateCount;
    uint remainingSplitBudget;
    uint splitOnlyCommitCount;
    uint allocatedNodeCount;
    uint budgetRejectedSplitCount;
    uint reservedCounter;
};

void main()
{
    // The next compaction must allocate its dense leaf output from slot zero.
    activeLeafCount = 0u;
}
)";
} // namespace

bool EnsureGpuRoamActiveLeafResetProgram(std::uint32_t& programId, std::string* errorMessage)
{
    return EnsureGpuRoamComputeProgram(
        programId,
        ActiveLeafResetComputeSource,
        "active leaf reset",
        errorMessage);
}

void RunGpuRoamActiveLeafResetPass(std::uint32_t programId, std::uint32_t counterBufferId)
{
    // This is a compute pass instead of glBufferSubData so OpenGL and D3D12 report
    // the same algorithm stage and no CPU upload is mislabeled as shader work.
    // The preceding split pass has completed behind an SSBO memory barrier.
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 3, counterBufferId);
    glUseProgram(programId);
    // One invocation is sufficient because only one shared counter is modified.
    glDispatchCompute(1U, 1U, 1U);
    // Final compaction reads activeLeafCount immediately in the next dispatch.
    glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
}
} // namespace ParallelRoam::Algorithms::GpuRoam
