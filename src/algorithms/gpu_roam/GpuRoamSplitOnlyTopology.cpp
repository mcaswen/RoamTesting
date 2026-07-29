#include "algorithms/gpu_roam/GpuRoamSplitOnlyTopology.h"

#include "algorithms/gpu_roam/GpuRoamComputeSupport.h"

#include <glad/gl.h>

#include <algorithm>

namespace ParallelRoam::Algorithms::GpuRoam
{
namespace
{
constexpr const char* SplitOnlyTopologyComputeSource = R"(
#version 430 core
// 一个 invocation 尝试提交一个 split candidate
// 多候选可能竞争同一 parent 或节点池尾指针
layout(local_size_x = 128) in;

struct NodeRecord
{
    // 本 pass 原地修改 topology flag child index 和 build id
    vec4 domainAAndB;
    vec4 domainCAndErrors;
    uvec4 topology0;
    uvec4 topology1;
    uvec4 pathAndCreatedBuild;
    uvec4 activatedAndSplitBuild;
    uvec4 mergeBuildAndDepth;
};

layout(std430, binding = 0) buffer NodeBuffer
{
    // 节点池容量固定，allocatedNodeCount 决定下一段空闲槽位
    NodeRecord nodes[];
};

layout(std430, binding = 3) buffer CounterBuffer
{
    // splitCandidateCount 是输入，commit 和 allocated 计数是输出
    uint activeLeafCount;
    uint splitCandidateCount;
    uint mergeCandidateCount;
    uint remainingSplitBudget;
    uint splitOnlyCommitCount;
    uint allocatedNodeCount;
    uint budgetRejectedSplitCount;
    uint reservedCounter;
};

layout(std430, binding = 4) readonly buffer SplitCandidateBuffer
{
    // 候选顺序由原子写入产生，不承诺按 error 排序
    uint splitCandidates[];
};

uniform uint uNodeCapacity;
uniform uint uMaxDepth;
uniform uint uBuildSequenceLow;
uniform uint uBuildSequenceHigh;

const uint invalidNode = 0xffffffffu;
const uint splitFlag = 1u << 0u;
const uint activeLeafFlag = 1u << 2u;
const uint forcedSplitFlag = 1u << 1u;

bool isActiveLeaf(uint nodeIndex)
{
    // active 和 split 两个位共同定义可提交 leaf
    uint flags = nodes[nodeIndex].topology1.w;
    return (flags & activeLeafFlag) != 0u && (flags & splitFlag) == 0u;
}

bool markParentSplit(uint nodeIndex, out uint originalFlags)
{
    // CAS 是 parent 的唯一认领点，避免两个 candidate 重复分配 child
    originalFlags = nodes[nodeIndex].topology1.w;
    if ((originalFlags & activeLeafFlag) == 0u || (originalFlags & splitFlag) != 0u)
    {
        return false;
    }

    uint splitFlags = (originalFlags | splitFlag) & ~activeLeafFlag;
    // 返回原值等于期望值才表示当前 invocation 获得提交权
    uint previous = atomicCompSwap(nodes[nodeIndex].topology1.w, originalFlags, splitFlags);
    return previous == originalFlags;
}

void restoreParentLeaf(uint nodeIndex, uint originalFlags)
{
    // 内存分配失败时只回滚仍处于本 invocation splitFlags 的 parent
    // 若状态已被其他合法写入改变，CAS 不覆盖新状态
    uint splitFlags = (originalFlags | splitFlag) & ~activeLeafFlag;
    atomicCompSwap(nodes[nodeIndex].topology1.w, splitFlags, originalFlags);
}

bool allocateNodes(uint count, out uint firstNode)
{
    // 使用 CAS 循环在固定容量节点池中预留连续 child 槽位
    // 八次重试限制高竞争下单个 invocation 的最坏耗时
    for (uint attempt = 0u; attempt < 8u; ++attempt)
    {
        // atomicAdd 零用于取得与其他原子操作有序的当前尾指针
        uint current = atomicAdd(allocatedNodeCount, 0u);
        if (current + count > uNodeCapacity)
        {
            return false;
        }

        // 只有尾指针未变化时才提交整段连续分配
        uint previous = atomicCompSwap(allocatedNodeCount, current, current + count);
        if (previous == current)
        {
            firstNode = current;
            return true;
        }
    }

    // 竞争失败不会消耗容量，调用方负责恢复 parent leaf 标记
    return false;
}

bool reserveSplitBudget(uint count)
{
    // remainingSplitBudget 表示还能增加多少个最终活动 leaf。
    // 单 parent split 净增一，完整 diamond split 净增二。
    for (uint attempt = 0u; attempt < 8u; ++attempt)
    {
        // 原子读和 CAS 共同防止并行 invocation 超卖 token。
        uint current = atomicAdd(remainingSplitBudget, 0u);
        if (current < count)
        {
            // 拒绝次数独立于节点池容量失败，供统一统计显示。
            atomicAdd(budgetRejectedSplitCount, 1u);
            return false;
        }
        uint previous = atomicCompSwap(remainingSplitBudget, current, current - count);
        if (previous == current)
        {
            return true;
        }
    }
    // 高竞争下有限重试失败也不能继续修改拓扑。
    atomicAdd(budgetRejectedSplitCount, 1u);
    return false;
}

void releaseSplitBudget(uint count)
{
    // parent 认领或 child 分配失败时必须归还完整预留量。
    atomicAdd(remainingSplitBudget, count);
}

void writeChildNode(
    uint childIndex,
    uint parentIndex,
    vec2 domainA,
    vec2 domainB,
    vec2 domainC,
    uint depth,
    uint chunkId,
    bool forced)
{
    // child 先在局部变量中完整初始化，再一次性写入节点池
    // 外侧 neighbor 暂设 invalid，当前 split-only 基线不传播完整兼容链
    NodeRecord child;
    child.domainAAndB = vec4(domainA, domainB);
    child.domainCAndErrors = vec4(domainC, 0.0, 0.0);
    child.topology0 = uvec4(parentIndex, invalidNode, invalidNode, invalidNode);
    child.topology1 = uvec4(invalidNode, invalidNode, chunkId, activeLeafFlag | (forced ? forcedSplitFlag : 0u));
    // path id 尚未在 GPU 路径重建，created/activated build 用于本轮可视化
    child.pathAndCreatedBuild = uvec4(0u, 0u, uBuildSequenceLow, uBuildSequenceHigh);
    child.activatedAndSplitBuild = uvec4(uBuildSequenceLow, uBuildSequenceHigh, 0u, 0u);
    child.mergeBuildAndDepth = uvec4(0u, 0u, depth, 0u);
    nodes[childIndex] = child;
}

void writeSplitChildren(uint parentIndex, uint firstChild, bool forced)
{
    // 最长边 A-B 中点产生两个 child，domain 顺序沿用 CPU DOD split
    NodeRecord parent = nodes[parentIndex];
    vec2 a = parent.domainAAndB.xy;
    vec2 b = parent.domainAAndB.zw;
    vec2 c = parent.domainCAndErrors.xy;
    vec2 midpoint = (a + b) * 0.5;
    uint childDepth = parent.mergeBuildAndDepth.z + 1u;
    uint chunkId = parent.topology1.z;

    // 两个连续槽位分别写 left/right child，随后 parent 才发布 child index
    writeChildNode(firstChild, parentIndex, c, a, midpoint, childDepth, chunkId, forced);
    writeChildNode(firstChild + 1u, parentIndex, b, c, midpoint, childDepth, chunkId, forced);
    // parent 已在 markParentSplit 中移出 active leaf，child 初始化后再建立可达引用
    nodes[parentIndex].topology0.y = firstChild;
    nodes[parentIndex].topology0.z = firstChild + 1u;
    nodes[parentIndex].activatedAndSplitBuild.z = uBuildSequenceLow;
    nodes[parentIndex].activatedAndSplitBuild.w = uBuildSequenceHigh;
}

void main()
{
    // splitCandidateCount 由前一 pass 原子生成，dispatch 尾部必须检查
    uint candidateSlot = gl_GlobalInvocationID.x;
    if (candidateSlot >= splitCandidateCount)
    {
        return;
    }

    uint nodeIndex = splitCandidates[candidateSlot];
    // 候选可能因并发认领或旧列表而失效，提交前重新验证物理边界和状态
    if (nodeIndex == invalidNode || nodeIndex >= uNodeCapacity || !isActiveLeaf(nodeIndex))
    {
        return;
    }

    NodeRecord candidate = nodes[nodeIndex];
    // 深度上限在 candidate pass 和 commit pass 双重检查
    if (candidate.mergeBuildAndDepth.z >= uMaxDepth)
    {
        return;
    }

    uint baseNeighbor = candidate.topology0.w;
    if (baseNeighbor == invalidNode)
    {
        // 地形边界没有配对三角形，可以独立分配两个 child
        // token 早于 parent flag 认领，失败路径按相反顺序回滚。
        if (!reserveSplitBudget(1u))
        {
            return;
        }
        uint originalFlags = 0u;
        if (!markParentSplit(nodeIndex, originalFlags))
        {
            releaseSplitBudget(1u);
            return;
        }

        uint firstChild = 0u;
        // 先认领 parent 再分配；容量不足时恢复原 leaf flags
        if (!allocateNodes(2u, firstChild))
        {
            restoreParentLeaf(nodeIndex, originalFlags);
            releaseSplitBudget(1u);
            return;
        }

        writeSplitChildren(nodeIndex, firstChild, false);
        atomicAdd(splitOnlyCommitCount, 1u);
        return;
    }

    // 只由较小 nodeIndex 提交一对 base neighbors，避免双方重复处理同一 diamond
    if (baseNeighbor <= nodeIndex || baseNeighbor >= uNodeCapacity)
    {
        return;
    }

    NodeRecord paired = nodes[baseNeighbor];
    // 当前基线只支持互为 base 且位于同一 chunk 的直接兼容对
    if (paired.topology0.w != nodeIndex ||
        paired.topology1.z != candidate.topology1.z ||
        paired.mergeBuildAndDepth.z >= uMaxDepth ||
        !isActiveLeaf(baseNeighbor))
    {
        return;
    }

    if (!reserveSplitBudget(2u))
    {
        return;
    }

    uint originalFlags = 0u;
    uint pairedOriginalFlags = 0u;
    // 两个 token 覆盖 diamond 两侧各自增加的一个活动 leaf。
    // 两个 parent 必须全部认领成功，否则不能发布半个 diamond
    if (!markParentSplit(nodeIndex, originalFlags))
    {
        releaseSplitBudget(2u);
        return;
    }

    // 第二个 parent 竞争失败时回滚第一个 parent
    if (!markParentSplit(baseNeighbor, pairedOriginalFlags))
    {
        restoreParentLeaf(nodeIndex, originalFlags);
        releaseSplitBudget(2u);
        return;
    }

    uint firstChild = 0u;
    // 四个 child 必须连续预留，容量不足时按逆序恢复两侧 parent
    if (!allocateNodes(4u, firstChild))
    {
        restoreParentLeaf(baseNeighbor, pairedOriginalFlags);
        restoreParentLeaf(nodeIndex, originalFlags);
        releaseSplitBudget(2u);
        return;
    }

    // facing parent 的 child 标记 forced，调试着色可区分兼容性补分裂
    writeSplitChildren(nodeIndex, firstChild, false);
    writeSplitChildren(baseNeighbor, firstChild + 2u, true);
    atomicAdd(splitOnlyCommitCount, 2u);
}
)";

bool EnsureSplitOnlyProgram(std::uint32_t& programId, std::string* errorMessage)
{
    // program 缓存在算法状态中，失败原因带 split-only 标签
    return EnsureGpuRoamComputeProgram(
        programId,
        SplitOnlyTopologyComputeSource,
        "split-only topology",
        errorMessage);
}
} // namespace

bool RunGpuRoamSplitOnlyTopologyPass(
    std::uint32_t& programId,
    const GpuRoamSplitOnlyTopologyPassInput& input,
    std::string* errorMessage)
{
    // topology pass 会原地修改节点池，缺少任一 buffer 都不能部分执行
    if (input.NodeBufferId == 0U ||
        input.SplitCandidateBufferId == 0U ||
        input.CounterBufferId == 0U ||
        input.NodeCapacity == 0U)
    {
        if (errorMessage != nullptr)
        {
            *errorMessage = "GPU ROAM-like split-only topology pass has incomplete buffers";
        }
        return false;
    }

    // 延迟编译让不支持 compute 的路径可以在能力检查阶段提前跳过
    if (!EnsureSplitOnlyProgram(programId, errorMessage))
    {
        return false;
    }

    // counter 与 node buffer 同时可写，candidate buffer 保持只读
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, input.NodeBufferId);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 3, input.CounterBufferId);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 4, input.SplitCandidateBufferId);

    glUseProgram(programId);
    SetGpuRoamProgramUInt(programId, "uNodeCapacity", static_cast<std::uint32_t>(input.NodeCapacity));
    SetGpuRoamProgramUInt(programId, "uMaxDepth", static_cast<std::uint32_t>(std::max(input.MaxDepth, 0)));
    SetGpuRoamProgramUInt(programId, "uBuildSequenceLow", GpuRoamLow32(input.BuildSequence));
    SetGpuRoamProgramUInt(programId, "uBuildSequenceHigh", GpuRoamHigh32(input.BuildSequence));
    // dispatch count 使用分配容量上限，shader 再读取实际 candidate counter
    glDispatchCompute(GpuRoamWorkGroupCount(input.CandidateDispatchCount), 1U, 1U);
    // 后续 compaction 读取新 flags/child，CPU 还会异步复制 counter
    glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT | GL_BUFFER_UPDATE_BARRIER_BIT);
    return true;
}
} // namespace ParallelRoam::Algorithms::GpuRoam
