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

// 与 error pass 保持完全相同，避免 split 和 merge 使用不同评分函数
const float projectedEdgeWeight = 0.20;
const float artificialMaximumScreenError = 3.402823466e+38;
const float projectionEpsilon = 1.0e-7;

float sampleHeight(vec2 uv)
{
    // 显式采用 CPU HeightMap::SampleBilinear 的 uv * (size - 1) 约定。
    // candidate 对 internal parent 重新评分，不能只复用 leaf error 数组。
    ivec2 size = textureSize(uHeightMap, 0);
    // clamp 后的端点恰好落到纹素 0 和 size - 1。
    vec2 pixel = clamp(uv, vec2(0.0), vec2(1.0)) * vec2(max(size - ivec2(1), ivec2(0)));
    ivec2 p0 = ivec2(floor(pixel));
    ivec2 p1 = min(p0 + ivec2(1), size - ivec2(1));
    vec2 weight = pixel - vec2(p0);
    // 显式插值确保 OpenGL 与 D3D12 的评分输入一致。
    float h00 = texelFetch(uHeightMap, p0, 0).r;
    float h10 = texelFetch(uHeightMap, ivec2(p1.x, p0.y), 0).r;
    float h01 = texelFetch(uHeightMap, ivec2(p0.x, p1.y), 0).r;
    float h11 = texelFetch(uHeightMap, p1, 0).r;
    return mix(mix(h00, h10, weight.x), mix(h01, h11, weight.x), weight.y);
}

vec3 domainToWorld(vec2 uv)
{
    // CPU 只上传 domain，世界空间位置始终由当前高度图求值
    return vec3(
        (uv.x - 0.5) * uTerrainSize,
        sampleHeight(uv) * uHeightScale,
        (uv.y - 0.5) * uTerrainSize);
}

bool isNodeVisible(NodeRecord node, vec3 a, vec3 b, vec3 c)
{
    // merge parent 和 split leaf 使用完全相同的保守可见性判断。
    vec3 minimumPoint = min(a, min(b, c));
    vec3 maximumPoint = max(a, max(b, c));
    float worldError = node.domainCAndErrors.z * uHeightScale;
    // nested wedgie thickness 扩张包围盒，避免子树高度偏差被误剔除。
    minimumPoint.y -= worldError;
    maximumPoint.y += worldError;
    vec3 center = (minimumPoint + maximumPoint) * 0.5;
    vec3 extents = (maximumPoint - minimumPoint) * 0.5;
    for (uint planeIndex = 0u; planeIndex < 6u; ++planeIndex)
    {
        // projectedRadius 是 AABB 在当前平面法线上的半径。
        vec4 plane = uFrustumPlanes[planeIndex];
        float centerDistance = dot(plane.xyz, center) + plane.w;
        float projectedRadius = dot(abs(plane.xyz), extents);
        if (centerDistance + projectedRadius < 0.0)
        {
            return false;
        }
    }
    return true;
}

bool wedgieIntersectsNearPlane(float worldError, vec3 a, vec3 b, vec3 c)
{
    // 固定索引 4 对应 CPU 构建的 inward near plane。
    vec4 nearPlane = uFrustumPlanes[4];
    // thickness 沿世界 Y 轴，abs(normal.y) 给出平面距离半径。
    float thicknessRadius = abs(nearPlane.y) * worldError;
    return dot(nearPlane.xyz, a) + nearPlane.w <= thicknessRadius ||
        dot(nearPlane.xyz, b) + nearPlane.w <= thicknessRadius ||
        dot(nearPlane.xyz, c) + nearPlane.w <= thicknessRadius;
}

float conservativeScreenDistortion(float worldError, vec3 a, vec3 b, vec3 c)
{
    if (wedgieIntersectsNearPlane(worldError, a, b, c))
    {
        return artificialMaximumScreenError;
    }

    // 以 w=0 变换方向，避免 view translation 污染 thickness。
    vec4 thicknessClip = uViewProjection * vec4(0.0, worldError, 0.0, 0.0);
    vec3 vertices[3] = vec3[3](a, b, c);
    float halfWidth = float(max(uDrawableWidth, 1u)) * 0.5;
    float halfHeight = float(max(uDrawableHeight, 1u)) * 0.5;
    float minimumDenominator = artificialMaximumScreenError;
    float maximumNumeratorSquared = 0.0;
    for (uint vertexIndex = 0u; vertexIndex < 3u; ++vertexIndex)
    {
        vec4 clip = uViewProjection * vec4(vertices[vertexIndex], 1.0);
        // 齐次 clip.w 形式把透视、正交和 API 深度约定统一到公式 (3)。
        float denominator = clip.w * clip.w - thicknessClip.w * thicknessClip.w;
        if (isnan(denominator) || isinf(denominator) || denominator <= projectionEpsilon)
        {
            return artificialMaximumScreenError;
        }
        // X/Y 分别使用 drawable 宽高，结果直接以 pixel 为单位。
        float horizontal = halfWidth * (thicknessClip.x * clip.w - thicknessClip.w * clip.x);
        float vertical = halfHeight * (thicknessClip.y * clip.w - thicknessClip.w * clip.y);
        float numeratorSquared = horizontal * horizontal + vertical * vertical;
        if (isnan(numeratorSquared) || isinf(numeratorSquared))
        {
            return artificialMaximumScreenError;
        }
        // 分开组合角点极值，避免 center depth 对粗三角形近端的低估。
        minimumDenominator = min(minimumDenominator, denominator);
        maximumNumeratorSquared = max(maximumNumeratorSquared, numeratorSquared);
    }
    return 2.0 * sqrt(maximumNumeratorSquared) / minimumDenominator;
}

float projectedLongestEdge(vec3 a, vec3 b, vec3 c)
{
    // edge-density 与 geometric bound 独立，只共享最终 max priority。
    vec3 vertices[3] = vec3[3](a, b, c);
    vec2 screenPositions[3];
    vec2 halfDrawable = vec2(
        float(max(uDrawableWidth, 1u)) * 0.5,
        float(max(uDrawableHeight, 1u)) * 0.5);
    for (uint vertexIndex = 0u; vertexIndex < 3u; ++vertexIndex)
    {
        vec4 clip = uViewProjection * vec4(vertices[vertexIndex], 1.0);
        if (isnan(clip.w) || isinf(clip.w) || abs(clip.w) <= projectionEpsilon)
        {
            return artificialMaximumScreenError;
        }
        screenPositions[vertexIndex] = halfDrawable * clip.xy / clip.w;
        if (any(isnan(screenPositions[vertexIndex])) || any(isinf(screenPositions[vertexIndex])))
        {
            return artificialMaximumScreenError;
        }
    }
    return max(
        max(length(screenPositions[0] - screenPositions[1]), length(screenPositions[1] - screenPositions[2])),
        length(screenPositions[2] - screenPositions[0]));
}

float scoreNode(uint nodeIndex)
{
    // 此实现必须与 GpuRoamErrorEvaluation 的 scoreNode 同步修改
    NodeRecord node = nodes[nodeIndex];
    vec2 aUv = node.domainAAndB.xy;
    vec2 bUv = node.domainAAndB.zw;
    vec2 cUv = node.domainCAndErrors.xy;

    vec3 a = domainToWorld(aUv);
    vec3 b = domainToWorld(bUv);
    vec3 c = domainToWorld(cUv);
    if (!isNodeVisible(node, a, b, c))
    {
        // 零分不会产生主动 split，并允许 DOD baseline 在本 Build 回收旧拓扑。
        return 0.0;
    }
    float worldError = node.domainCAndErrors.z * uHeightScale;
    float geometricBoundPixels = conservativeScreenDistortion(worldError, a, b, c);
    if (geometricBoundPixels == artificialMaximumScreenError)
    {
        return geometricBoundPixels;
    }
    // max 语义与 CPU ROAM 保持一致，不把 geometric bound 和 edge-density 重复累加。
    return max(geometricBoundPixels, projectedLongestEdge(a, b, c) * projectedEdgeWeight);
}

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
)";
} // namespace

bool EnsureGpuRoamCandidateMarkingProgram(
    std::uint32_t& programId,
    std::string* errorMessage)
{
    // split 和 merge 共享一个 program，确保同帧阈值和评分参数一致
    return EnsureGpuRoamComputeProgram(
        programId,
        CandidateMarkingComputeSource,
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
