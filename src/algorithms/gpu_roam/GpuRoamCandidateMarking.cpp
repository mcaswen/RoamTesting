#include "algorithms/gpu_roam/GpuRoamCandidateMarking.h"

#include "algorithms/gpu_roam/GpuRoamComputeSupport.h"
#include "algorithms/gpu_roam/GpuRoamShaderCommon.h"

#include <glad/gl.h>

#include <algorithm>
#include <string>

namespace ParallelRoam::Algorithms::GpuRoam
{
namespace
{
const std::string CandidateMarkingComputeSource = std::string{R"glsl(
#version 430 core
// 同一 dispatch 同时扫描 active leaf 和完整 node pool
// invocation 数量取两个范围的最大值
layout(local_size_x = 128) in;

struct NodeRecord
{
    // candidate pass 读取 domain error topology flag 和 depth
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
    // split 分支按 active index 访问，merge 分支按物理 index 扫描
    NodeRecord nodes[];
};

layout(std430, binding = 1) readonly buffer ActiveLeafBuffer
{
    // split 只允许从已压缩的当前活动叶集合产生
    uint activeLeafIndices[];
};

layout(std430, binding = 2) readonly buffer ScreenErrorBuffer
{
    // error 数组与 activeLeafIndices 使用相同 slot 顺序
    float screenErrors[];
};

layout(std430, binding = 3) buffer CounterBuffer
{
    // activeLeafCount 由 compaction 写入，本 pass 不修改
    uint activeLeafCount;
    // 两个 candidate counter 在 dispatch 前必须清零
    uint splitCandidateCount;
    uint mergeCandidateCount;
    uint remainingSplitBudget;
};

layout(std430, binding = 4) writeonly buffer SplitCandidateBuffer
{
    // 候选列表保存物理 nodeIndex，topology pass 可直接访问节点池
    uint splitCandidates[];
};

layout(std430, binding = 5) writeonly buffer MergeCandidateBuffer
{
    // 当前桥接版本只输出 merge 统计，不在 GPU 提交 merge
    uint mergeCandidates[];
};

layout(binding = 0) uniform sampler2D uHeightMap;

uniform uint uNodeCount;
uniform uint uActiveLeafLimit;
uniform uint uMaxDepth;
uniform float uTerrainSize;
uniform float uHeightScale;
uniform float uSplitThreshold;
uniform float uMergeThreshold;
uniform mat4 uViewProjection;
uniform vec4 uFrustumPlanes[6];
uniform uint uDrawableWidth;
uniform uint uDrawableHeight;
uniform uint uCandidateKind;

 )glsl"} +
    std::string{GpuRoamScoreCommonGlsl} +
    R"glsl(
void main()
{
    uint index = gl_GlobalInvocationID.x;

    // split 与 merge 分开 dispatch，使两个 ROAM 决策阶段可独立计时。
    if (uCandidateKind == 0u)
    {
        if (index >= uActiveLeafLimit)
        {
            return;
        }
        uint nodeIndex = activeLeafIndices[index];
        uint depth = nodes[nodeIndex].mergeBuildAndDepth.z;
        float screenError = screenErrors[index];
        // 最大深度是硬约束，达到后即使误差很高也不能继续分配节点
        if (depth < uMaxDepth && screenError > uSplitThreshold)
        {
            // 原子 slot 只保证唯一，不保证候选按误差排序
            uint outputIndex = atomicAdd(splitCandidateCount, 1u);
            splitCandidates[outputIndex] = nodeIndex;
        }
        return;
    }

    // merge 分支按完整 CPU 快照节点池扫描 split parent。当前 GPU-like
    // 路径只记录候选，真正的 merge 拓扑提交仍由 CPU DOD baseline 完成。
    if (index >= uNodeCount)
    {
        return;
    }

    const uint splitFlag = 1u << 0u;
    uint flags = nodes[index].topology1.w;
    // leaf 没有 child 可回收，直接跳过
    if ((flags & splitFlag) == 0u)
    {
        return;
    }

    float mergeScore = scoreNode(index);
    // mergeThreshold 被 C++ 限制不高于 splitThreshold，形成稳定迟滞区间
    if (mergeScore <= uMergeThreshold)
    {
        uint outputIndex = atomicAdd(mergeCandidateCount, 1u);
        mergeCandidates[outputIndex] = index;
    }
}
)glsl";
} // namespace

bool EnsureGpuRoamCandidateMarkingProgram(
    std::uint32_t& programId,
    std::string* errorMessage)
{
    // split 和 merge 共享一个 program，确保同帧阈值和评分参数一致
    return EnsureGpuRoamComputeProgram(
        programId,
        CandidateMarkingComputeSource.c_str(),
        "candidate marking",
        errorMessage);
}

void RunGpuRoamCandidateMarkingPass(const GpuRoamCandidateMarkingPassInput& input)
{
    // binding 0 到 5 对应 shader 中连续的拓扑评估数据流
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, input.NodeBufferId);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, input.ActiveLeafBufferId);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 2, input.ScreenErrorBufferId);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 3, input.CounterBufferId);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 4, input.SplitCandidateBufferId);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 5, input.MergeCandidateBufferId);
    // internal parent merge 评分会采样高度图，因此该 pass 仍需绑定纹理
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, input.HeightMapTextureId);

    glUseProgram(input.ProgramId);
    SetGpuRoamProgramInt(input.ProgramId, "uHeightMap", 0);
    SetGpuRoamProgramUInt(input.ProgramId, "uNodeCount", static_cast<std::uint32_t>(input.NodeCount));
    SetGpuRoamProgramUInt(input.ProgramId, "uActiveLeafLimit", static_cast<std::uint32_t>(input.ActiveLeafLimit));
    SetGpuRoamProgramUInt(input.ProgramId, "uMaxDepth", static_cast<std::uint32_t>(std::max(input.MaxDepth, 0)));
    SetGpuRoamProgramFloat(input.ProgramId, "uTerrainSize", input.TerrainSize);
    SetGpuRoamProgramFloat(input.ProgramId, "uHeightScale", input.HeightScale);
    SetGpuRoamProgramFloat(input.ProgramId, "uSplitThreshold", input.SplitThreshold);
    // C++ 再次钳制 merge 阈值，防止 UI 或 benchmark 输入破坏 hysteresis
    SetGpuRoamProgramFloat(input.ProgramId, "uMergeThreshold", std::min(input.MergeThreshold, input.SplitThreshold));
    SetGpuRoamProgramMat4(input.ProgramId, "uViewProjection", input.ViewProjection);
    SetGpuRoamProgramVec4Array(
        input.ProgramId,
        "uFrustumPlanes",
        input.FrustumPlanes.data(),
        input.FrustumPlanes.size());
    SetGpuRoamProgramUInt(input.ProgramId, "uDrawableWidth", input.DrawableWidth);
    SetGpuRoamProgramUInt(input.ProgramId, "uDrawableHeight", input.DrawableHeight);
    SetGpuRoamProgramUInt(
        input.ProgramId,
        "uCandidateKind",
        static_cast<std::uint32_t>(input.Kind));
    const std::size_t dispatchCount = input.Kind == GpuRoamCandidateKind::Split
        ? input.ActiveLeafLimit
        : input.NodeCount;
    glDispatchCompute(GpuRoamWorkGroupCount(dispatchCount), 1U, 1U);
    // topology pass 和异步 counter readback 都依赖本 pass 的 SSBO 写入
    glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
}
} // namespace ParallelRoam::Algorithms::GpuRoam
