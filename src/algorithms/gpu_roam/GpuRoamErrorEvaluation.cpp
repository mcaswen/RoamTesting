#include "algorithms/gpu_roam/GpuRoamErrorEvaluation.h"

#include "algorithms/gpu_roam/GpuRoamComputeSupport.h"
#include "algorithms/gpu_roam/GpuRoamShaderCommon.h"

#include <glad/gl.h>

#include <string>

namespace ParallelRoam::Algorithms::GpuRoam
{
namespace
{
const std::string ErrorEvaluationComputeSource = std::string{R"glsl(
#version 430 core
// 一个 invocation 对应活动列表中的一个 leaf slot
// 物理 nodeIndex 由 ActiveLeafBuffer 间接取得
layout(local_size_x = 128) in;

struct NodeRecord
{
    // domain 与 CPU 快照共享 std430 布局
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
    // 只读取 domain 和预计算 geometric error
    NodeRecord nodes[];
};

layout(std430, binding = 1) readonly buffer ActiveLeafBuffer
{
    // 稠密 slot 屏蔽节点池中的 inactive 和 split parent
    uint activeLeafIndices[];
};

layout(std430, binding = 2) writeonly buffer ScreenErrorBuffer
{
    // 输出按 leaf slot 排列，candidate pass 使用相同 slot 读取
    float screenErrors[];
};

layout(binding = 0) uniform sampler2D uHeightMap;

uniform uint uActiveLeafCount;
uniform float uTerrainSize;
uniform float uHeightScale;
uniform mat4 uViewProjection;
uniform vec4 uFrustumPlanes[6];
uniform uint uDrawableWidth;
uniform uint uDrawableHeight;

 )glsl"} +
    std::string{GpuRoamScoreCommonGlsl} +
    R"glsl(
void main()
{
    // dispatch 向上取整，尾部 invocation 必须显式退出
    uint leafSlot = gl_GlobalInvocationID.x;
    if (leafSlot >= uActiveLeafCount)
    {
        return;
    }

    // 输出仍使用 leafSlot，后续 pass 无需再次压缩或排序
    uint nodeIndex = activeLeafIndices[leafSlot];
    screenErrors[leafSlot] = scoreNode(nodeIndex);
}
)glsl";
} // namespace

bool EnsureGpuRoamErrorEvaluationProgram(
    std::uint32_t& programId,
    std::string* errorMessage)
{
    // 入口名用于将编译错误定位到误差 pass，而不是只报告通用 compute 失败
    return EnsureGpuRoamComputeProgram(
        programId,
        ErrorEvaluationComputeSource.c_str(),
        "error evaluation",
        errorMessage);
}

void RunGpuRoamErrorEvaluationPass(const GpuRoamErrorEvaluationPassInput& input)
{
    // 三个 SSBO binding 构成该 pass 的完整读写边界
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, input.NodeBufferId);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, input.ActiveLeafBufferId);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 2, input.ScreenErrorBufferId);
    // 高度图固定占用纹理单元零，与 uHeightMap uniform 保持一致
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, input.HeightMapTextureId);

    // 所有评分参数逐 dispatch 写入，避免依赖上一个算法或场景残留 uniform
    glUseProgram(input.ProgramId);
    SetGpuRoamProgramInt(input.ProgramId, "uHeightMap", 0);
    SetGpuRoamProgramUInt(input.ProgramId, "uActiveLeafCount", static_cast<std::uint32_t>(input.ActiveLeafCount));
    SetGpuRoamProgramFloat(input.ProgramId, "uTerrainSize", input.TerrainSize);
    SetGpuRoamProgramFloat(input.ProgramId, "uHeightScale", input.HeightScale);
    SetGpuRoamProgramMat4(input.ProgramId, "uViewProjection", input.ViewProjection);
    SetGpuRoamProgramVec4Array(
        input.ProgramId,
        "uFrustumPlanes",
        input.FrustumPlanes.data(),
        input.FrustumPlanes.size());
    SetGpuRoamProgramUInt(input.ProgramId, "uDrawableWidth", input.DrawableWidth);
    SetGpuRoamProgramUInt(input.ProgramId, "uDrawableHeight", input.DrawableHeight);
    // dispatch 只覆盖活动 leaf 数量，不按完整节点池容量浪费工作
    glDispatchCompute(GpuRoamWorkGroupCount(input.ActiveLeafCount), 1U, 1U);
    // candidate pass 随后读取 screenErrors，SSBO barrier 建立写后读顺序
    glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
}
} // namespace ParallelRoam::Algorithms::GpuRoam
