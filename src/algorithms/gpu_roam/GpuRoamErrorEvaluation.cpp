#include "algorithms/gpu_roam/GpuRoamErrorEvaluation.h"

#include "algorithms/gpu_roam/GpuRoamComputeSupport.h"

#include <glad/gl.h>

namespace ParallelRoam::Algorithms::GpuRoam
{
namespace
{
constexpr const char* ErrorEvaluationComputeSource = R"(
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
uniform float uDistanceScale;
uniform vec3 uCameraPosition;

// 下限避免零距离尺度放大为无穷
const float minimumDistanceScale = 0.01;
// 长边项补偿低高度差平坦区域的屏幕覆盖
const float projectedEdgeWeight = 0.20;

float sampleHeight(vec2 uv)
{
    // clamp 保证有限差分和边界节点不会访问高度图外部
    return texture(uHeightMap, clamp(uv, vec2(0.0), vec2(1.0))).r;
}

vec3 domainToWorld(vec2 uv)
{
    // domain 使用零到一 UV，世界地形以原点为中心铺在 XZ 平面
    return vec3(
        (uv.x - 0.5) * uTerrainSize,
        sampleHeight(uv) * uHeightScale,
        (uv.y - 0.5) * uTerrainSize);
}

float distanceWeight(float distanceToCamera)
{
    // 平方反比让近处误差快速增长，同时保持与 CPU baseline 相近的趋势
    float safeDistanceScale = max(uDistanceScale, minimumDistanceScale);
    float normalizedDistance = safeDistanceScale / distanceToCamera;
    return normalizedDistance * normalizedDistance;
}

float scoreNode(uint nodeIndex)
{
    // 三个 domain 点在当前高度图上重新求值，避免上传完整世界空间顶点
    NodeRecord node = nodes[nodeIndex];
    vec2 aUv = node.domainAAndB.xy;
    vec2 bUv = node.domainAAndB.zw;
    vec2 cUv = node.domainCAndErrors.xy;

    vec3 a = domainToWorld(aUv);
    vec3 b = domainToWorld(bUv);
    vec3 c = domainToWorld(cUv);
    // 中心距离作为视点权重，最小距离防止相机穿过地形时爆炸
    vec3 center = (a + b + c) / 3.0;
    float distanceToCamera = max(length(center - uCameraPosition), 0.05);
    float worldError = node.domainCAndErrors.z * uHeightScale;
    // 最长边项使近处平坦大三角形仍可被细分
    float longestEdgeLength = max(max(length(a - b), length(b - c)), length(c - a));
    float distanceScale = max(uDistanceScale, minimumDistanceScale);
    float weight = distanceWeight(distanceToCamera);
    float heightErrorScore = worldError * weight;
    float edgeLengthScore = longestEdgeLength * projectedEdgeWeight / distanceScale * weight;
    // 取最大值而非求和，便于分别解释高度误差和覆盖率触发原因
    return max(heightErrorScore, edgeLengthScore);
}

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
)";
} // namespace

bool EnsureGpuRoamErrorEvaluationProgram(
    std::uint32_t& programId,
    std::string* errorMessage)
{
    // 入口名用于将编译错误定位到误差 pass，而不是只报告通用 compute 失败
    return EnsureGpuRoamComputeProgram(
        programId,
        ErrorEvaluationComputeSource,
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
    SetGpuRoamProgramFloat(input.ProgramId, "uDistanceScale", input.DistanceScale);
    SetGpuRoamProgramVec3(input.ProgramId, "uCameraPosition", input.CameraPosition);
    // dispatch 只覆盖活动 leaf 数量，不按完整节点池容量浪费工作
    glDispatchCompute(GpuRoamWorkGroupCount(input.ActiveLeafCount), 1U, 1U);
    // candidate pass 随后读取 screenErrors，SSBO barrier 建立写后读顺序
    glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
}
} // namespace ParallelRoam::Algorithms::GpuRoam
