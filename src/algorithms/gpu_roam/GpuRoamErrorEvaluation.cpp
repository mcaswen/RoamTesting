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
uniform mat4 uView;
uniform vec4 uFrustumPlanes[6];
uniform float uProjectionScaleY;
uniform uint uDrawableHeight;
uniform uint uIsOrthographic;

// 与 CPU ROAM 评分保持相同的近裁深度下限
const float minimumViewDepth = 0.05;
// 长边项补偿低高度差平坦区域的屏幕覆盖
const float projectedEdgeWeight = 0.20;

float sampleHeight(vec2 uv)
{
    // CPU 采样把端点 UV 精确映射到首尾纹素中心。
    // 直接 texture() 会采用 API 的半纹素坐标约定，内部点会与 CPU 不同。
    ivec2 size = textureSize(uHeightMap, 0);
    // size - 1 对应 HeightMap::SampleBilinear 的像素跨度。
    vec2 pixel = clamp(uv, vec2(0.0), vec2(1.0)) * vec2(max(size - ivec2(1), ivec2(0)));
    ivec2 p0 = ivec2(floor(pixel));
    ivec2 p1 = min(p0 + ivec2(1), size - ivec2(1));
    vec2 weight = pixel - vec2(p0);
    // 四次 texelFetch 避免硬件过滤再次施加坐标偏移。
    float h00 = texelFetch(uHeightMap, p0, 0).r;
    float h10 = texelFetch(uHeightMap, ivec2(p1.x, p0.y), 0).r;
    float h01 = texelFetch(uHeightMap, ivec2(p0.x, p1.y), 0).r;
    float h11 = texelFetch(uHeightMap, p1, 0).r;
    return mix(mix(h00, h10, weight.x), mix(h01, h11, weight.x), weight.y);
}

vec3 domainToWorld(vec2 uv)
{
    // domain 使用零到一 UV，世界地形以原点为中心铺在 XZ 平面
    return vec3(
        (uv.x - 0.5) * uTerrainSize,
        sampleHeight(uv) * uHeightScale,
        (uv.y - 0.5) * uTerrainSize);
}

bool isNodeVisible(NodeRecord node, vec3 a, vec3 b, vec3 c)
{
    // 三个角点先形成当前线性三角形的世界空间 AABB。
    vec3 minimumPoint = min(a, min(b, c));
    vec3 maximumPoint = max(a, max(b, c));
    // 完整方差是整个子树的高度误差上界，用它扩张 Y 轴保持剔除保守。
    float worldError = node.domainCAndErrors.z * uHeightScale;
    minimumPoint.y -= worldError;
    maximumPoint.y += worldError;
    vec3 center = (minimumPoint + maximumPoint) * 0.5;
    vec3 extents = (maximumPoint - minimumPoint) * 0.5;
    for (uint planeIndex = 0u; planeIndex < 6u; ++planeIndex)
    {
        // 平面法线朝内，AABB 最大支撑点仍为负才表示完全在视锥外。
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

float scoreNode(uint nodeIndex)
{
    // GPU 节点直接携带 CPU 完整方差树传播后的 GeometricError。
    // 三个 domain 点在当前高度图上重新求值，避免上传完整世界空间顶点
    NodeRecord node = nodes[nodeIndex];
    vec2 aUv = node.domainAAndB.xy;
    vec2 bUv = node.domainAAndB.zw;
    vec2 cUv = node.domainCAndErrors.xy;

    vec3 a = domainToWorld(aUv);
    vec3 b = domainToWorld(bUv);
    vec3 c = domainToWorld(cUv);
    if (!isNodeVisible(node, a, b, c))
    {
        // 视锥外 leaf 不主动细分，CPU DOD baseline 仍负责兼容性 closure
        return 0.0;
    }

    vec3 center = (a + b + c) / 3.0;
    float worldError = node.domainCAndErrors.z * uHeightScale;
    // 平坦区域仍由最长边的投影覆盖率驱动继续细分。
    float longestEdgeLength = max(max(length(a - b), length(b - c)), length(c - a));
    // CPU 与 GPU 都以三角形中心的 view-space Z 近似整片深度。
    vec4 viewCenter = uView * vec4(center, 1.0);
    // 正交投影的像素密度与深度无关，透视投影才除以深度。
    float depthScale = uIsOrthographic != 0u ? 1.0 : max(abs(viewCenter.z), minimumViewDepth);
    // projectionScaleY 已包含 FOV；drawable height 把 NDC 尺度换算为像素。
    float pixelsPerWorldUnit = float(max(uDrawableHeight, 1u)) * 0.5 * abs(uProjectionScaleY) / depthScale;
    float heightErrorPixels = worldError * pixelsPerWorldUnit;
    float edgeLengthPixels = longestEdgeLength * pixelsPerWorldUnit * projectedEdgeWeight;
    return max(heightErrorPixels, edgeLengthPixels);
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
    SetGpuRoamProgramMat4(input.ProgramId, "uView", input.View);
    SetGpuRoamProgramVec4Array(
        input.ProgramId,
        "uFrustumPlanes",
        input.FrustumPlanes.data(),
        input.FrustumPlanes.size());
    SetGpuRoamProgramFloat(input.ProgramId, "uProjectionScaleY", input.ProjectionScaleY);
    SetGpuRoamProgramUInt(input.ProgramId, "uDrawableHeight", input.DrawableHeight);
    SetGpuRoamProgramUInt(input.ProgramId, "uIsOrthographic", input.IsOrthographic ? 1U : 0U);
    // dispatch 只覆盖活动 leaf 数量，不按完整节点池容量浪费工作
    glDispatchCompute(GpuRoamWorkGroupCount(input.ActiveLeafCount), 1U, 1U);
    // candidate pass 随后读取 screenErrors，SSBO barrier 建立写后读顺序
    glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
}
} // namespace ParallelRoam::Algorithms::GpuRoam
