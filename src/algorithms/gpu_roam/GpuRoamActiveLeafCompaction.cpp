#include "algorithms/gpu_roam/GpuRoamActiveLeafCompaction.h"

#include "algorithms/gpu_roam/GpuRoamComputeSupport.h"

#include <glad/gl.h>

namespace ParallelRoam::Algorithms::GpuRoam
{
namespace
{
constexpr const char* ActiveLeafCompactionComputeSource = R"(
#version 430 core
// 一个 invocation 检查一个物理节点槽位
// workgroup 宽度必须与 C++ 的 GpuRoamWorkGroupCount 约定一致
layout(local_size_x = 128) in;

struct NodeRecord
{
    // 三个 domain 顶点和误差值沿用 CPU 快照的 std430 打包顺序
    vec4 domainAAndB;
    vec4 domainCAndErrors;
    // topology0 保存 parent child 和 base neighbor
    uvec4 topology0;
    // topology1.w 保存 split active-leaf 等位标记
    uvec4 topology1;
    // 三组 build id 用于拒绝跨构建版本的陈旧节点
    uvec4 pathAndCreatedBuild;
    uvec4 activatedAndSplitBuild;
    uvec4 mergeBuildAndDepth;
};

layout(std430, binding = 0) readonly buffer NodeBuffer
{
    // 节点池可能包含已分配但当前不活动的历史槽位
    NodeRecord nodes[];
};

layout(std430, binding = 1) writeonly buffer ActiveLeafBuffer
{
    // 输出按原子分配的稠密 slot 保存物理节点索引
    uint activeLeafIndices[];
};

layout(std430, binding = 3) buffer CounterBuffer
{
    // activeLeafCount 在 dispatch 前由 reset pass 清零
    uint activeLeafCount;
    // 其余字段属于后续 pass，本入口只读取 allocatedNodeCount
    uint splitCandidateCount;
    uint mergeCandidateCount;
    uint remainingSplitBudget;
    uint splitOnlyCommitCount;
    uint allocatedNodeCount;
    uint budgetRejectedSplitCount;
    uint reservedCounter;
};

uniform uint uNodeCount;

void main()
{
    // CPU 容量和 GPU 实际已分配尾指针取较小值
    // 这样未初始化的预留槽位不会进入活动集合
    uint nodeIndex = gl_GlobalInvocationID.x;
    uint readableNodeCount = min(uNodeCount, allocatedNodeCount);
    if (nodeIndex >= readableNodeCount)
    {
        return;
    }

    // split parent 不再是可绘制叶节点，即使历史 active 位仍未清理也必须排除
    const uint splitFlag = 1u << 0u;
    const uint activeLeafFlag = 1u << 2u;
    uint flags = nodes[nodeIndex].topology1.w;
    if ((flags & activeLeafFlag) == 0u || (flags & splitFlag) != 0u)
    {
        return;
    }

    // 原子返回值是本 invocation 独占的稠密输出位置
    // 节点遍历顺序不保证稳定，但活动集合语义保持一致
    uint outputIndex = atomicAdd(activeLeafCount, 1u);
    activeLeafIndices[outputIndex] = nodeIndex;
}
)";
} // namespace

bool EnsureGpuRoamActiveLeafCompactionProgram(
    std::uint32_t& programId,
    std::string* errorMessage)
{
    // programId 非零时复用已链接程序，shader 不在普通帧重复编译
    return EnsureGpuRoamComputeProgram(
        programId,
        ActiveLeafCompactionComputeSource,
        "active leaf compaction",
        errorMessage);
}

void RunGpuRoamActiveLeafCompactionPass(const GpuRoamActiveLeafCompactionPassInput& input)
{
    // binding 必须与内嵌 GLSL 完全一致，后续 pass 会直接消费同一 active leaf buffer
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, input.NodeBufferId);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, input.ActiveLeafBufferId);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 3, input.CounterBufferId);

    // dispatch 覆盖 CPU 声明容量，shader 再通过 allocatedNodeCount 缩小可读范围
    glUseProgram(input.ProgramId);
    SetGpuRoamProgramUInt(input.ProgramId, "uNodeCount", static_cast<std::uint32_t>(input.NodeCount));
    glDispatchCompute(GpuRoamWorkGroupCount(input.NodeCount), 1U, 1U);
    // 后续 error pass 读取活动索引和计数器，必须等待 SSBO 写入可见
    glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
}
} // namespace ParallelRoam::Algorithms::GpuRoam
