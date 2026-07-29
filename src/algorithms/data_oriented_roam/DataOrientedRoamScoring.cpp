#include "algorithms/data_oriented_roam/DataOrientedRoamState.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace ParallelRoam::Algorithms::DataOrientedRoam
{
namespace
{
constexpr float MinimumViewDepth = 0.05F;
constexpr float ProjectedEdgeWeight = 0.20F;

float BuildVarianceSubtree(
    DataOrientedRoamState& state,
    const TriangleDomain& domain,
    int depth,
    std::size_t varianceIndex,
    std::vector<float>& varianceTree)
{
    const float localError = ComputeLocalGeometricError(state, domain);
    float subtreeError = localError;
    if (depth < state.Settings.MaxDepth)
    {
        const TriangleDomainChildren children = SplitTriangleDomain(domain);
        const float leftError = BuildVarianceSubtree(
            state,
            children.Left,
            depth + 1,
            varianceIndex * 2U + 1U,
            varianceTree);
        const float rightError = BuildVarianceSubtree(
            state,
            children.Right,
            depth + 1,
            varianceIndex * 2U + 2U,
            varianceTree);
        subtreeError = std::max({localError, leftError, rightError});
    }

    varianceTree[varianceIndex] = subtreeError;
    return subtreeError;
}

bool IsNodeVisible(
    const DataOrientedRoamState& state,
    DataOrientedRoamNodeConstRef node,
    const glm::vec3& a,
    const glm::vec3& b,
    const glm::vec3& c)
{
    glm::vec3 minimum = glm::min(a, glm::min(b, c));
    glm::vec3 maximum = glm::max(a, glm::max(b, c));
    const float worldError = node.GeometricError * state.HeightScale;
    minimum.y -= worldError;
    maximum.y += worldError;
    const glm::vec3 center = (minimum + maximum) * 0.5F;
    const glm::vec3 extents = (maximum - minimum) * 0.5F;

    for (const glm::vec4& plane : state.FrustumPlanes)
    {
        const glm::vec3 normal{plane};
        const float centerDistance = glm::dot(normal, center) + plane.w;
        const float projectedRadius = glm::dot(glm::abs(normal), extents);
        if (centerDistance + projectedRadius < 0.0F)
        {
            return false;
        }
    }
    return true;
}
} // namespace

TriangleDomainChildren SplitTriangleDomain(const TriangleDomain& domain)
{
    const glm::vec2 midpoint = (domain.A + domain.B) * 0.5F;
    return TriangleDomainChildren{
        TriangleDomain{domain.C, domain.A, midpoint},
        TriangleDomain{domain.B, domain.C, midpoint},
    };
}

bool ShouldSplitWithScore(
    const DataOrientedRoamState& state,
    DataOrientedRoamNodeConstRef node,
    float screenErrorScore)
{
    if (node.Depth >= state.Settings.MaxDepth)
    {
        return false;
    }

    if (screenErrorScore > state.Settings.SplitThreshold)
    {
        // 高于 split 阈值时直接展开
        return true;
    }

    if (screenErrorScore < state.Settings.MergeThreshold)
    {
        // 低于 merge 阈值时明确不 split
        // 中间区间才交给 hysteresis 保持稳定
        return false;
    }

    // hysteresis 区间沿用上一帧 split 状态
    // 避免相机轻微移动造成频繁 split / merge 抖动
    return WasSplitLastFrame(state, node);
}

bool WasSplitLastFrame(const DataOrientedRoamState& state, DataOrientedRoamNodeConstRef node)
{
    // hysteresis 只看上一帧最终 active split path
    return state.PreviousSplitPaths.find(node.PathId) != state.PreviousSplitPaths.end();
}

DataOrientedRoamLeafDebugClass ClassifyLeafDebug(
    const DataOrientedRoamState& state,
    DataOrientedRoamNodeConstRef node)
{
    // Rebuilt 同时覆盖新 split child 和本帧 merge 回来的 parent
    // debug color 用它突出本帧拓扑变化区域
    if (node.ActivatedBuildId == state.BuildSequence || node.MergeBuildId == state.BuildSequence)
    {
        return DataOrientedRoamLeafDebugClass::Rebuilt;
    }

    if (node.Depth > 0)
    {
        // 非 root leaf 但本帧未变化时归为历史细分
        return DataOrientedRoamLeafDebugClass::Subdivided;
    }

    return DataOrientedRoamLeafDebugClass::Original;
}

glm::vec3 DebugColorForLeaf(const DataOrientedRoamState& state, DataOrientedRoamNodeConstRef node)
{
    const float depthRatio = std::clamp(
        static_cast<float>(node.Depth) / static_cast<float>(std::max(state.Settings.MaxDepth, 1)),
        0.0F,
        1.0F);

    switch (ClassifyLeafDebug(state, node))
    {
    case DataOrientedRoamLeafDebugClass::Original:
        return glm::vec3{0.28F, 0.34F, 0.30F};
    case DataOrientedRoamLeafDebugClass::Subdivided:
        return glm::mix(glm::vec3{0.08F, 0.72F, 0.62F}, glm::vec3{0.10F, 0.34F, 0.95F}, depthRatio);
    case DataOrientedRoamLeafDebugClass::Rebuilt:
        // forced split 高亮 crack repair 传播路径
        if (node.ActivatedByForcedSplit)
        {
            return glm::mix(glm::vec3{0.96F, 0.34F, 0.90F}, glm::vec3{0.96F, 0.16F, 0.42F}, depthRatio);
        }

        // 普通 rebuild 使用暖色表示本帧主动拓扑变化
        return glm::mix(glm::vec3{1.0F, 0.68F, 0.15F}, glm::vec3{1.0F, 0.34F, 0.10F}, depthRatio);
    }

    return glm::vec3{0.28F, 0.34F, 0.30F};
}

float DebugHighlightForLeaf(const DataOrientedRoamState& state, DataOrientedRoamNodeConstRef node)
{
    switch (ClassifyLeafDebug(state, node))
    {
    case DataOrientedRoamLeafDebugClass::Original:
        return 0.35F;
    case DataOrientedRoamLeafDebugClass::Subdivided:
        return 0.70F;
    case DataOrientedRoamLeafDebugClass::Rebuilt:
        return 1.0F;
    }

    return 0.35F;
}

float ComputeLocalGeometricError(const DataOrientedRoamState& state, const TriangleDomain& domain)
{
    // 误差缓存只看 domain 对应的高度变化
    // 因此 node 创建后可以跨帧复用
    const float heightA = state.HeightMap->SampleBilinear(domain.A.x, domain.A.y);
    const float heightB = state.HeightMap->SampleBilinear(domain.B.x, domain.B.y);
    const float heightC = state.HeightMap->SampleBilinear(domain.C.x, domain.C.y);

    const auto edgeMidpointError = [&state](const glm::vec2& start, const glm::vec2& end, float startHeight, float endHeight) {
        // 边中点误差能捕获边界起伏
        // 只看三角形重心会漏掉沿边的高频变化
        const glm::vec2 midpoint = (start + end) * 0.5F;
        const float midpointHeight = state.HeightMap->SampleBilinear(midpoint.x, midpoint.y);
        const float interpolatedHeight = (startHeight + endHeight) * 0.5F;
        return std::abs(midpointHeight - interpolatedHeight);
    };

    const glm::vec2 centroid = (domain.A + domain.B + domain.C) / 3.0F;
    // 重心采样补足三角形内部起伏
    const float centroidHeight = state.HeightMap->SampleBilinear(centroid.x, centroid.y);
    const float centroidInterpolatedHeight = (heightA + heightB + heightC) / 3.0F;

    // 取边中点和重心的最大误差
    // 平衡边界裂缝风险和三角形内部起伏
    return std::max({
        edgeMidpointError(domain.A, domain.B, heightA, heightB),
        edgeMidpointError(domain.B, domain.C, heightB, heightC),
        edgeMidpointError(domain.C, domain.A, heightC, heightA),
        std::abs(centroidHeight - centroidInterpolatedHeight),
    });
}

void RebuildVarianceTrees(DataOrientedRoamState& state)
{
    const std::size_t nodeCountPerTree =
        (std::size_t{1} << static_cast<unsigned>(state.Settings.MaxDepth + 1)) - 1U;
    for (std::vector<float>& tree : state.VarianceTrees)
    {
        tree.assign(nodeCountPerTree, 0.0F);
    }

    const TriangleDomain rootA{
        glm::vec2{0.0F, 1.0F},
        glm::vec2{1.0F, 0.0F},
        glm::vec2{0.0F, 0.0F},
    };
    const TriangleDomain rootB{
        glm::vec2{1.0F, 0.0F},
        glm::vec2{0.0F, 1.0F},
        glm::vec2{1.0F, 1.0F},
    };
    static_cast<void>(BuildVarianceSubtree(state, rootA, 0, 0U, state.VarianceTrees[0]));
    static_cast<void>(BuildVarianceSubtree(state, rootB, 0, 0U, state.VarianceTrees[1]));
    state.VarianceHeightMap = state.HeightMap;
    state.VarianceTreeMaxDepth = state.Settings.MaxDepth;
}

void RefreshNodeVarianceErrors(DataOrientedRoamState& state)
{
    for (std::size_t node = 0U; node < state.Nodes.size(); ++node)
    {
        state.Nodes.GeometricErrors[node] = VarianceError(
            state,
            state.Nodes.VarianceTreeIndices[node],
            state.Nodes.VarianceIndices[node]);
    }
}

float VarianceError(
    const DataOrientedRoamState& state,
    std::uint8_t varianceTreeIndex,
    std::size_t varianceIndex)
{
    const std::size_t treeIndex = static_cast<std::size_t>(varianceTreeIndex);
    if (treeIndex >= state.VarianceTrees.size() || varianceIndex >= state.VarianceTrees[treeIndex].size())
    {
        return 0.0F;
    }
    return state.VarianceTrees[treeIndex][varianceIndex];
}

float ComputeScreenErrorScore(const DataOrientedRoamState& state, DataOrientedRoamNodeConstRef node)
{
    const glm::vec3 a = DomainToWorld(state, node.Domain.A);
    const glm::vec3 b = DomainToWorld(state, node.Domain.B);
    const glm::vec3 c = DomainToWorld(state, node.Domain.C);
    if (!IsNodeVisible(state, node, a, b, c))
    {
        // 视锥外节点不主动细分，forced split 仍可维持边界拓扑
        return 0.0F;
    }

    const glm::vec3 center = (a + b + c) / 3.0F;
    const float worldError = node.GeometricError * state.HeightScale;
    const float longestEdgeLength = std::max({
        glm::length(a - b),
        glm::length(b - c),
        glm::length(c - a),
    });
    const glm::vec4 viewCenter = state.View * glm::vec4{center, 1.0F};
    const float projectionScaleY = std::abs(state.Projection[1][1]);
    const float halfDrawableHeight = static_cast<float>(state.DrawableHeight) * 0.5F;
    const bool isOrthographic = std::abs(state.Projection[3][3] - 1.0F) <= std::numeric_limits<float>::epsilon();
    const float depthScale = isOrthographic ? 1.0F : std::max(std::abs(viewCenter.z), MinimumViewDepth);
    const float pixelsPerWorldUnit = halfDrawableHeight * projectionScaleY / depthScale;
    const float heightErrorPixels = worldError * pixelsPerWorldUnit;
    const float edgeLengthPixels = longestEdgeLength * pixelsPerWorldUnit * ProjectedEdgeWeight;
    return std::max(heightErrorPixels, edgeLengthPixels);
}

glm::vec3 DomainToWorld(const DataOrientedRoamState& state, const glm::vec2& uv)
{
    // 地形中心放在世界原点
    // 这和规则网格 builder 保持同一坐标系
    const float height = state.HeightMap->SampleBilinear(uv.x, uv.y);
    return glm::vec3{
        (uv.x - 0.5F) * state.TerrainSize,
        height * state.HeightScale,
        (uv.y - 0.5F) * state.TerrainSize,
    };
}

glm::vec3 SampleNormal(const DataOrientedRoamState& state, const glm::vec2& uv)
{
    // 法线从 HeightMap 梯度估计，不依赖相邻 leaf
    const float stepU = 1.0F / static_cast<float>(std::max(state.HeightMap->Width() - 1, 1));
    const float stepV = 1.0F / static_cast<float>(std::max(state.HeightMap->Height() - 1, 1));
    const float left = state.HeightMap->SampleBilinear(uv.x - stepU, uv.y);
    const float right = state.HeightMap->SampleBilinear(uv.x + stepU, uv.y);
    const float down = state.HeightMap->SampleBilinear(uv.x, uv.y - stepV);
    const float up = state.HeightMap->SampleBilinear(uv.x, uv.y + stepV);

    const glm::vec3 tangentX{stepU * 2.0F * state.TerrainSize, (right - left) * state.HeightScale, 0.0F};
    const glm::vec3 tangentZ{0.0F, (up - down) * state.HeightScale, stepV * 2.0F * state.TerrainSize};
    const glm::vec3 normal = glm::cross(tangentZ, tangentX);

    if (glm::dot(normal, normal) <= std::numeric_limits<float>::epsilon())
    {
        // 极端平坦或退化采样时回退竖直法线
        return glm::vec3{0.0F, 1.0F, 0.0F};
    }

    return glm::normalize(normal);
}
} // 命名空间 ParallelRoam::Algorithms::DataOrientedRoam
