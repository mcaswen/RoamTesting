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
    // 下一次 compaction 必须从槽位 0 开始分配 dense leaf 输出
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
    // 这里使用 compute pass，而不是 glBufferSubData
    // 这样 OpenGL 和 D3D12 都能统计相同算法阶段，且不会把 CPU 上传误记为 shader 工作
    // 前一个 split pass 已在 SSBO memory barrier 后完成
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 3, counterBufferId);
    glUseProgram(programId);
    // 只修改一个共享计数器，因此一次 invocation 就足够
    glDispatchCompute(1U, 1U, 1U);
    // 下一次 dispatch 会立即读取 activeLeafCount 完成最终 compaction
    glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
}
} // namespace ParallelRoam::Algorithms::GpuRoam
