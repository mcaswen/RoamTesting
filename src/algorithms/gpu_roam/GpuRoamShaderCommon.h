#pragma once

#include <string_view>

namespace ParallelRoam::Algorithms::GpuRoam
{
// 两个 OpenGL ROAM compute pass 共用的高度采样、可见性和屏幕误差公式。
// 该片段依赖调用方已经声明 NodeRecord、nodes 以及下列 uniform：
// uHeightMap、uTerrainSize、uHeightScale、uViewProjection、uFrustumPlanes、
// uDrawableWidth 和 uDrawableHeight。
inline constexpr std::string_view GpuRoamScoreCommonGlsl = R"glsl(
// 长边项补偿低高度差平坦区域的屏幕覆盖
const float projectedEdgeWeight = 0.20;
const float artificialMaximumScreenError = 3.402823466e+38;
const float projectionEpsilon = 1.0e-7;

float sampleHeight(vec2 uv)
{
    // 显式采用 CPU HeightMap::SampleBilinear 的 uv * (size - 1) 约定。
    ivec2 size = textureSize(uHeightMap, 0);
    // clamp 保证边界 UV 不会访问纹理范围外的像素。
    vec2 pixel = clamp(uv, vec2(0.0), vec2(1.0)) * vec2(max(size - ivec2(1), ivec2(0)));
    // p0/p1 和 weight 对应 CPU 双线性采样的整数坐标与小数权重。
    ivec2 p0 = ivec2(floor(pixel));
    ivec2 p1 = min(p0 + ivec2(1), size - ivec2(1));
    vec2 weight = pixel - vec2(p0);
    // 使用 texelFetch 明确复现 CPU 的四点双线性插值。
    // 不使用 texture()，避免硬件过滤的半纹素约定造成 CPU/GPU 偏差。
    float h00 = texelFetch(uHeightMap, p0, 0).r;
    float h10 = texelFetch(uHeightMap, ivec2(p1.x, p0.y), 0).r;
    float h01 = texelFetch(uHeightMap, ivec2(p0.x, p1.y), 0).r;
    float h11 = texelFetch(uHeightMap, p1, 0).r;
    return mix(mix(h00, h10, weight.x), mix(h01, h11, weight.x), weight.y);
}

vec3 domainToWorld(vec2 uv)
{
    // domain 的 X/Z 来自 UV，Y 来自当前高度图和运行时高度缩放。
    return vec3(
        (uv.x - 0.5) * uTerrainSize,
        sampleHeight(uv) * uHeightScale,
        (uv.y - 0.5) * uTerrainSize);
}

bool isNodeVisible(NodeRecord node, vec3 a, vec3 b, vec3 c)
{
    // 误差使用 world-space 高度单位，与 CPU scoring 的 HeightScale 口径一致。
    vec3 minimumPoint = min(a, min(b, c));
    vec3 maximumPoint = max(a, max(b, c));
    // 用 nested wedgie 的世界高度误差扩张 AABB，避免错误剔除子树。
    float worldError = node.domainCAndErrors.z * uHeightScale;
    minimumPoint.y -= worldError;
    maximumPoint.y += worldError;
    vec3 center = (minimumPoint + maximumPoint) * 0.5;
    vec3 extents = (maximumPoint - minimumPoint) * 0.5;
    // 对每个平面使用 AABB 支撑半径，只有完全在某个平面外才剔除。
    for (uint planeIndex = 0u; planeIndex < 6u; ++planeIndex)
    {
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
    // CPU 和 GPU 都约定 near plane 位于固定索引 4。
    vec4 nearPlane = uFrustumPlanes[4];
    // thickness 只沿世界 Y 轴，因此平面距离半径取法线 Y 分量。
    float thicknessRadius = abs(nearPlane.y) * worldError;
    return dot(nearPlane.xyz, a) + nearPlane.w <= thicknessRadius ||
        dot(nearPlane.xyz, b) + nearPlane.w <= thicknessRadius ||
        dot(nearPlane.xyz, c) + nearPlane.w <= thicknessRadius;
}

float conservativeScreenDistortion(float worldError, vec3 a, vec3 b, vec3 c)
{
    // near plane 交叉时不能使用普通投影公式，必须保守地给出最大分数。
    if (wedgieIntersectsNearPlane(worldError, a, b, c))
    {
        return artificialMaximumScreenError;
    }

    // w=0 只变换 thickness 方向，避免 view translation 污染公式。
    vec4 thicknessClip = uViewProjection * vec4(0.0, worldError, 0.0, 0.0);
    vec3 vertices[3] = vec3[3](a, b, c);
    // drawable 的半尺寸把 NDC 结果换算成像素。
    float halfWidth = float(max(uDrawableWidth, 1u)) * 0.5;
    float halfHeight = float(max(uDrawableHeight, 1u)) * 0.5;
    float minimumDenominator = artificialMaximumScreenError;
    float maximumNumeratorSquared = 0.0;
    for (uint vertexIndex = 0u; vertexIndex < 3u; ++vertexIndex)
    {
        vec4 clip = uViewProjection * vec4(vertices[vertexIndex], 1.0);
        // 分母和分子分别对应论文公式 (3) 的透视除法项。
        float denominator = clip.w * clip.w - thicknessClip.w * thicknessClip.w;
        if (isnan(denominator) || isinf(denominator) || denominator <= projectionEpsilon)
        {
            return artificialMaximumScreenError;
        }
        float horizontal = halfWidth * (thicknessClip.x * clip.w - thicknessClip.w * clip.x);
        float vertical = halfHeight * (thicknessClip.y * clip.w - thicknessClip.w * clip.y);
        float numeratorSquared = horizontal * horizontal + vertical * vertical;
        if (isnan(numeratorSquared) || isinf(numeratorSquared))
        {
            return artificialMaximumScreenError;
        }
        minimumDenominator = min(minimumDenominator, denominator);
        maximumNumeratorSquared = max(maximumNumeratorSquared, numeratorSquared);
    }
    // 最大分子和最小分母允许来自不同角点，保持整片 wedgie 的保守上界。
    return 2.0 * sqrt(maximumNumeratorSquared) / minimumDenominator;
}

float projectedLongestEdge(vec3 a, vec3 b, vec3 c)
{
    // 该项只用于平坦区域的网格密度，不替代 nested wedgie 几何误差。
    vec3 vertices[3] = vec3[3](a, b, c);
    vec2 screenPositions[3];
    // 三个角点投影到像素平面后取最长边，作为额外密度项。
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
    // scoreNode 是 error evaluation 和 candidate marking 共用的评分入口。
    NodeRecord node = nodes[nodeIndex];
    // 节点只保存 UV domain，世界坐标在 shader 内按当前 HeightMap 重建。
    vec2 aUv = node.domainAAndB.xy;
    vec2 bUv = node.domainAAndB.zw;
    vec2 cUv = node.domainCAndErrors.xy;

    vec3 a = domainToWorld(aUv);
    vec3 b = domainToWorld(bUv);
    vec3 c = domainToWorld(cUv);
    if (!isNodeVisible(node, a, b, c))
    {
        // 视锥外节点不主动占用 split 预算，但仍由上层拓扑闭包处理边界。
        return 0.0;
    }

    float worldError = node.domainCAndErrors.z * uHeightScale;
    // 几何误差触碰 near plane 时返回人工最大优先级，否则与长边项取最大值。
    float geometricBoundPixels = conservativeScreenDistortion(worldError, a, b, c);
    if (geometricBoundPixels == artificialMaximumScreenError)
    {
        return geometricBoundPixels;
    }
    return max(geometricBoundPixels, projectedLongestEdge(a, b, c) * projectedEdgeWeight);
}
)glsl";
} // namespace ParallelRoam::Algorithms::GpuRoam
