#include "algorithms/classic_roam/ClassicRoamMeshBuilder.h"

#include "algorithms/RoamNestedWedgie.h"
#include "algorithms/RoamScreenProjection.h"
#include "algorithms/ITerrainLodAlgorithm.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace ParallelRoam::Algorithms::ClassicRoam
{
namespace
{
constexpr float ProjectedEdgeWeight = 0.20F;
} // namespace

TriangleDomainChildren SplitTriangleDomain(const TriangleDomain& domain)
{
    // A/B 始终是 base edge，C 是 apex；两个 child 继续保持逆时针绕序
    const glm::vec2 midpoint = (domain.A + domain.B) * 0.5F;
    return TriangleDomainChildren{
        TriangleDomain{domain.C, domain.A, midpoint},
        TriangleDomain{domain.B, domain.C, midpoint},
    };
}

bool ClassicRoamMeshBuilder::ShouldSplit(const ClassicRoamNode& node) const
{
    // 最大深度限制优先于误差判断，避免相机贴近时无限细分
    if (node.Depth >= _settings.MaxDepth)
    {
        return false;
    }

    return ShouldSplitWithScore(node, ComputeScreenErrorScore(node));
}

bool ClassicRoamMeshBuilder::ShouldSplitWithScore(const ClassicRoamNode& node, float screenErrorScore) const
{
    if (node.Depth >= _settings.MaxDepth)
    {
        return false;
    }

    if (screenErrorScore > _settings.SplitThreshold)
    {
        // 明确高于 split 阈值时不走 hysteresis
        return true;
    }

    if (screenErrorScore < _settings.MergeThreshold)
    {
        return false;
    }

    // hysteresis 区间沿用上一帧 split 状态，降低 split/merge 抖动
    // 这也是 fixed camera benchmark 稳定的重要条件
    return WasSplitLastFrame(node);
}

bool ClassicRoamMeshBuilder::WasSplitLastFrame(const ClassicRoamNode& node) const
{
    return _previousSplitPaths.find(node.PathId) != _previousSplitPaths.end();
}

ClassicRoamMeshBuilder::LeafDebugClass ClassicRoamMeshBuilder::ClassifyLeafDebug(const ClassicRoamNode& node) const
{
    if (node.ActivatedBuildId == _buildSequence || node.MergeBuildId == _buildSequence)
    {
        // 本帧新激活和 merge 回来的 parent 都属于 rebuilt
        return LeafDebugClass::Rebuilt;
    }

    if (node.Depth > 0)
    {
        return LeafDebugClass::Subdivided;
    }

    return LeafDebugClass::Original;
}

glm::vec3 ClassicRoamMeshBuilder::DebugColorForLeaf(const ClassicRoamNode& node) const
{
    const float depthRatio = std::clamp(
        static_cast<float>(node.Depth) / static_cast<float>(std::max(_settings.MaxDepth, 1)),
        0.0F,
        1.0F);

    switch (ClassifyLeafDebug(node))
    {
    case LeafDebugClass::Original:
        return glm::vec3{0.28F, 0.34F, 0.30F};
    case LeafDebugClass::Subdivided:
        return glm::mix(glm::vec3{0.08F, 0.72F, 0.62F}, glm::vec3{0.10F, 0.34F, 0.95F}, depthRatio);
    case LeafDebugClass::Rebuilt:
        // forced split 用粉色系标出 crack repair 触发区域
        if (node.ActivatedByForcedSplit)
        {
            return glm::mix(glm::vec3{0.96F, 0.34F, 0.90F}, glm::vec3{0.96F, 0.16F, 0.42F}, depthRatio);
        }

        // 普通 rebuild 用暖色，便于和历史细分叶子区分
        return glm::mix(glm::vec3{1.0F, 0.68F, 0.15F}, glm::vec3{1.0F, 0.34F, 0.10F}, depthRatio);
    }

    return glm::vec3{0.28F, 0.34F, 0.30F};
}

float ClassicRoamMeshBuilder::DebugHighlightForLeaf(const ClassicRoamNode& node) const
{
    // highlight 与 color 分类保持同源，避免 UI debug 语义分裂
    switch (ClassifyLeafDebug(node))
    {
    case LeafDebugClass::Original:
        return 0.35F;
    case LeafDebugClass::Subdivided:
        return 0.70F;
    case LeafDebugClass::Rebuilt:
        return 1.0F;
    }

    return 0.35F;
}

float ClassicRoamMeshBuilder::ComputeBaseMidpointDisplacement(const TriangleDomain& domain) const
{
    const float heightA = _heightMap->SampleBilinear(domain.A.x, domain.A.y);
    const float heightB = _heightMap->SampleBilinear(domain.B.x, domain.B.y);
    const glm::vec2 midpoint = (domain.A + domain.B) * 0.5F;
    const float midpointHeight = _heightMap->SampleBilinear(midpoint.x, midpoint.y);
    const float interpolatedHeight = (heightA + heightB) * 0.5F;
    return midpointHeight - interpolatedHeight;
}

void ClassicRoamMeshBuilder::RebuildVarianceTrees(int finestDepth)
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
    const auto signedDisplacement = [this](const TriangleDomain& domain) {
        return ComputeBaseMidpointDisplacement(domain);
    };
    static_cast<void>(Roam::BuildNestedWedgieTree(
        rootA,
        finestDepth,
        _varianceTrees[0],
        splitDomain,
        signedDisplacement));
    static_cast<void>(Roam::BuildNestedWedgieTree(
        rootB,
        finestDepth,
        _varianceTrees[1],
        splitDomain,
        signedDisplacement));
    _varianceHeightMap = _heightMap;
    _varianceTreeMaxDepth = finestDepth;
}

void ClassicRoamMeshBuilder::RefreshNodeVarianceErrors()
{
    for (const std::unique_ptr<ClassicRoamNode>& node : _nodes)
    {
        node->GeometricError = VarianceError(node->VarianceTreeIndex, node->VarianceIndex);
    }
}

float ClassicRoamMeshBuilder::VarianceError(std::uint8_t varianceTreeIndex, std::size_t varianceIndex) const
{
    const std::size_t treeIndex = static_cast<std::size_t>(varianceTreeIndex);
    if (treeIndex >= _varianceTrees.size() || varianceIndex >= _varianceTrees[treeIndex].size())
    {
        return 0.0F;
    }

    return _varianceTrees[treeIndex][varianceIndex];
}

float ClassicRoamMeshBuilder::ComputeScreenErrorScore(const ClassicRoamNode& node) const
{
    const glm::vec3 a = DomainToWorld(node.Domain.A);
    const glm::vec3 b = DomainToWorld(node.Domain.B);
    const glm::vec3 c = DomainToWorld(node.Domain.C);
    if (!IsNodeVisible(node, a, b, c))
    {
        // 视锥外节点不主动占用细分预算；forced split 仍可为可见边界维持无裂缝拓扑
        return 0.0F;
    }

    const float worldError = node.GeometricError * _heightScale;
    const std::array<glm::vec3, 3U> triangle{a, b, c};
    const std::size_t nearPlaneIndex = static_cast<std::size_t>(TerrainLodFrustumPlane::Near);
    const float geometricBoundPixels = Roam::ComputeConservativeScreenDistortionPixels({
        triangle,
        _viewProjection,
        _frustumPlanes[nearPlaneIndex],
        worldError,
        _drawableWidth,
        _drawableHeight,
    });
    if (geometricBoundPixels == Roam::ArtificialMaximumScreenError)
    {
        // 论文要求 wedgie 触碰或穿越 near plane 时跳过公式并给人工最大 priority。
        return geometricBoundPixels;
    }

    // edge-density 是项目额外质量项，不属于论文 geometric distortion bound。
    const float edgeDensityPixels = Roam::ComputeProjectedLongestEdgePixels(
        triangle,
        _viewProjection,
        _drawableWidth,
        _drawableHeight) * ProjectedEdgeWeight;
    return std::max(geometricBoundPixels, edgeDensityPixels);
}

bool ClassicRoamMeshBuilder::IsNodeVisible(
    const ClassicRoamNode& node,
    const glm::vec3& a,
    const glm::vec3& b,
    const glm::vec3& c) const
{
    glm::vec3 minimum = glm::min(a, glm::min(b, c));
    glm::vec3 maximum = glm::max(a, glm::max(b, c));
    // nested wedgie thickness 界定子树累积高度偏差，扩张后测试保持保守
    const float worldError = node.GeometricError * _heightScale;
    minimum.y -= worldError;
    maximum.y += worldError;
    const glm::vec3 center = (minimum + maximum) * 0.5F;
    const glm::vec3 extents = (maximum - minimum) * 0.5F;

    for (const glm::vec4& plane : _frustumPlanes)
    {
        const glm::vec3 normal{plane};
        const float centerDistance = glm::dot(normal, center) + plane.w;
        const float projectedRadius = glm::dot(glm::abs(normal), extents);
        if (centerDistance + projectedRadius < 0.0F)
        {
            // inward plane 的最大 AABB 支撑点仍在外侧，整个节点都不可见
            return false;
        }
    }
    return true;
}

glm::vec3 ClassicRoamMeshBuilder::DomainToWorld(const glm::vec2& uv) const
{
    // 世界空间仍以地形中心为原点，方便复用相机和光照
    const float height = _heightMap->SampleBilinear(uv.x, uv.y);
    return glm::vec3{
        (uv.x - 0.5F) * _terrainSize,
        height * _heightScale,
        (uv.y - 0.5F) * _terrainSize,
    };
}

glm::vec3 ClassicRoamMeshBuilder::SampleNormal(const glm::vec2& uv) const
{
    // 法线从 Height Map 梯度估计，不依赖相邻 leaf 拓扑
    const float stepU = 1.0F / static_cast<float>(std::max(_heightMap->Width() - 1, 1));
    const float stepV = 1.0F / static_cast<float>(std::max(_heightMap->Height() - 1, 1));
    const float left = _heightMap->SampleBilinear(uv.x - stepU, uv.y);
    const float right = _heightMap->SampleBilinear(uv.x + stepU, uv.y);
    const float down = _heightMap->SampleBilinear(uv.x, uv.y - stepV);
    const float up = _heightMap->SampleBilinear(uv.x, uv.y + stepV);

    const glm::vec3 tangentX{stepU * 2.0F * _terrainSize, (right - left) * _heightScale, 0.0F};
    const glm::vec3 tangentZ{0.0F, (up - down) * _heightScale, stepV * 2.0F * _terrainSize};
    const glm::vec3 normal = glm::cross(tangentZ, tangentX);

    // 极端退化时回退到竖直法线，避免 shader 中出现 NaN
    if (glm::dot(normal, normal) <= std::numeric_limits<float>::epsilon())
    {
        return glm::vec3{0.0F, 1.0F, 0.0F};
    }

    return glm::normalize(normal);
}
} // 命名空间 ParallelRoam::Algorithms::ClassicRoam
