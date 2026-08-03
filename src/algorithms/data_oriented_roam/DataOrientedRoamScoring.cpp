#include "algorithms/data_oriented_roam/DataOrientedRoamState.h"

#include "algorithms/RoamNestedWedgie.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace ParallelRoam::Algorithms::DataOrientedRoam
{
namespace
{
constexpr float MinimumViewDepth = 0.05F;
constexpr float ProjectedEdgeWeight = 0.20F;

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

float ComputeBaseMidpointDisplacement(const DataOrientedRoamState& state, const TriangleDomain& domain)
{
    const float heightA = state.HeightMap->SampleBilinear(domain.A.x, domain.A.y);
    const float heightB = state.HeightMap->SampleBilinear(domain.B.x, domain.B.y);
    const glm::vec2 midpoint = (domain.A + domain.B) * 0.5F;
    const float midpointHeight = state.HeightMap->SampleBilinear(midpoint.x, midpoint.y);
    const float interpolatedHeight = (heightA + heightB) * 0.5F;
    return midpointHeight - interpolatedHeight;
}

void RebuildVarianceTrees(DataOrientedRoamState& state, int finestDepth)
{
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
    const auto splitDomain = [](const TriangleDomain& domain) {
        return SplitTriangleDomain(domain);
    };
    const auto signedDisplacement = [&state](const TriangleDomain& domain) {
        return ComputeBaseMidpointDisplacement(state, domain);
    };
    static_cast<void>(Roam::BuildNestedWedgieTree(
        rootA,
        finestDepth,
        state.VarianceTrees[0],
        splitDomain,
        signedDisplacement));
    static_cast<void>(Roam::BuildNestedWedgieTree(
        rootB,
        finestDepth,
        state.VarianceTrees[1],
        splitDomain,
        signedDisplacement));
    state.VarianceHeightMap = state.HeightMap;
    state.VarianceTreeMaxDepth = finestDepth;
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
