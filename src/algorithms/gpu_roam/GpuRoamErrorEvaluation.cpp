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
uniform mat4 uViewProjection;
uniform vec4 uFrustumPlanes[6];
uniform uint uDrawableWidth;
uniform uint uDrawableHeight;

// 长边项补偿低高度差平坦区域的屏幕覆盖
const float projectedEdgeWeight = 0.20;
const float artificialMaximumScreenError = 3.402823466e+38;
const float projectionEpsilon = 1.0e-7;

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
    // nested wedgie thickness 是子树累积高度误差界，用它扩张 Y 轴保持剔除保守。
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

bool wedgieIntersectsNearPlane(float worldError, vec3 a, vec3 b, vec3 c)
{
    // TerrainLodFrustumPlane::Near 的固定索引为 4；平面法线朝向视锥内部。
    vec4 nearPlane = uFrustumPlanes[4];
    // 世界 thickness 沿 Y 轴，投影到 near-plane normal 后得到有符号距离半径。
    float thicknessRadius = abs(nearPlane.y) * worldError;
    // 平面距离和 thickness 都是仿射量，三角形内部最小值必在角点取得。
    return dot(nearPlane.xyz, a) + nearPlane.w <= thicknessRadius ||
        dot(nearPlane.xyz, b) + nearPlane.w <= thicknessRadius ||
        dot(nearPlane.xyz, c) + nearPlane.w <= thicknessRadius;
}

float conservativeScreenDistortion(float worldError, vec3 a, vec3 b, vec3 c)
{
    if (wedgieIntersectsNearPlane(worldError, a, b, c))
    {
        // 论文要求 wedgie 触碰或穿越 near plane 时跳过公式并设人工最大优先级。
        return artificialMaximumScreenError;
    }

    // direction 的 w=0，确保平移不影响论文 camera-space thickness vector。
    vec4 thicknessClip = uViewProjection * vec4(0.0, worldError, 0.0, 0.0);
    vec3 vertices[3] = vec3[3](a, b, c);
    // NDC [-1,1] 的半 drawable 尺度把公式结果转换成像素。
    float halfWidth = float(max(uDrawableWidth, 1u)) * 0.5;
    float halfHeight = float(max(uDrawableHeight, 1u)) * 0.5;
    float minimumDenominator = artificialMaximumScreenError;
    float maximumNumeratorSquared = 0.0;
    for (uint vertexIndex = 0u; vertexIndex < 3u; ++vertexIndex)
    {
        vec4 clip = uViewProjection * vec4(vertices[vertexIndex], 1.0);
        // clip.w 与 thicknessClip.w 分别对应公式 (3) 的 r 与 c。
        float denominator = clip.w * clip.w - thicknessClip.w * thicknessClip.w;
        if (isnan(denominator) || isinf(denominator) || denominator <= projectionEpsilon)
        {
            return artificialMaximumScreenError;
        }
        // 分子分别乘 X/Y 像素尺度，支持非方形 drawable 和任意 aspect。
        float horizontal = halfWidth * (thicknessClip.x * clip.w - thicknessClip.w * clip.x);
        float vertical = halfHeight * (thicknessClip.y * clip.w - thicknessClip.w * clip.y);
        float numeratorSquared = horizontal * horizontal + vertical * vertical;
        if (isnan(numeratorSquared) || isinf(numeratorSquared))
        {
            return artificialMaximumScreenError;
        }
        // 最大分子和最小分母允许来自不同角点，这是保守性的关键。
        minimumDenominator = min(minimumDenominator, denominator);
        maximumNumeratorSquared = max(maximumNumeratorSquared, numeratorSquared);
    }
    return 2.0 * sqrt(maximumNumeratorSquared) / minimumDenominator;
}

float projectedLongestEdge(vec3 a, vec3 b, vec3 c)
{
    // 透视变换保持直线，边的屏幕长度可由两个投影端点直接得到。
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
    // GPU 节点直接携带 CPU 按论文公式 (1) 传播后的 nested wedgie thickness。
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

    float worldError = node.domainCAndErrors.z * uHeightScale;
    // 公式 (3) 分别取三个角点上的最小分母和最大分子，得到整片 wedgie 的保守像素上界。
    float geometricBoundPixels = conservativeScreenDistortion(worldError, a, b, c);
    if (geometricBoundPixels == artificialMaximumScreenError)
    {
        return geometricBoundPixels;
    }
    // edge-density 继续作为项目额外项，但不再复用中心深度近似。
    float edgeDensityPixels = projectedLongestEdge(a, b, c) * projectedEdgeWeight;
    return max(geometricBoundPixels, edgeDensityPixels);
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
