#include "algorithms/classic_roam/ClassicRoamMeshBuilder.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace ParallelRoam::Algorithms::ClassicRoam
{
namespace
{
constexpr float MinimumViewDepth = 0.05F;
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

float ClassicRoamMeshBuilder::ComputeLocalGeometricError(const TriangleDomain& domain) const
{
    const float heightA = _heightMap->SampleBilinear(domain.A.x, domain.A.y);
    const float heightB = _heightMap->SampleBilinear(domain.B.x, domain.B.y);
    const float heightC = _heightMap->SampleBilinear(domain.C.x, domain.C.y);

    // 边中点误差能捕获边界起伏，避免只看 base edge
    const auto edgeMidpointError = [this](const glm::vec2& start, const glm::vec2& end, float startHeight, float endHeight) {
        const glm::vec2 midpoint = (start + end) * 0.5F;
        const float midpointHeight = _heightMap->SampleBilinear(midpoint.x, midpoint.y);
        const float interpolatedHeight = (startHeight + endHeight) * 0.5F;
        return std::abs(midpointHeight - interpolatedHeight);
    };

    const glm::vec2 centroid = (domain.A + domain.B + domain.C) / 3.0F;
    // 重心采样补足三角形内部的非边界起伏
    const float centroidHeight = _heightMap->SampleBilinear(centroid.x, centroid.y);
    const float centroidInterpolatedHeight = (heightA + heightB + heightC) / 3.0F;

    // 采样三条边中点和重心，避免非 base 边或三角形内部起伏被漏掉
    return std::max({
        edgeMidpointError(domain.A, domain.B, heightA, heightB),
        edgeMidpointError(domain.B, domain.C, heightB, heightC),
        edgeMidpointError(domain.C, domain.A, heightC, heightA),
        std::abs(centroidHeight - centroidInterpolatedHeight),
    });
}

void ClassicRoamMeshBuilder::RebuildVarianceTrees()
{
    const std::size_t nodeCountPerTree = (std::size_t{1} << static_cast<unsigned>(_settings.MaxDepth + 1)) - 1U;
    for (std::vector<float>& tree : _varianceTrees)
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
    static_cast<void>(BuildVarianceSubtree(rootA, 0, 0, _varianceTrees[0]));
    static_cast<void>(BuildVarianceSubtree(rootB, 0, 0, _varianceTrees[1]));
    _varianceHeightMap = _heightMap;
    _varianceTreeMaxDepth = _settings.MaxDepth;
}

float ClassicRoamMeshBuilder::BuildVarianceSubtree(
    const TriangleDomain& domain,
    int depth,
    std::size_t varianceIndex,
    std::vector<float>& varianceTree)
{
    const float localError = ComputeLocalGeometricError(domain);
    float subtreeError = localError;
    if (depth < _settings.MaxDepth)
    {
        const TriangleDomainChildren children = SplitTriangleDomain(domain);
        const float leftError = BuildVarianceSubtree(children.Left, depth + 1, varianceIndex * 2U + 1U, varianceTree);
        const float rightError = BuildVarianceSubtree(children.Right, depth + 1, varianceIndex * 2U + 2U, varianceTree);
        subtreeError = std::max({localError, leftError, rightError});
    }

    varianceTree[varianceIndex] = subtreeError;
    return subtreeError;
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
    const glm::vec3 center = (a + b + c) / 3.0F;
    const float worldError = node.GeometricError * _heightScale;
    const float longestEdgeLength = std::max({
        glm::length(a - b),
        glm::length(b - c),
        glm::length(c - a),
    });
    const glm::vec4 viewCenter = _view * glm::vec4{center, 1.0F};
    const float projectionScaleY = std::abs(_projection[1][1]);
    const float halfDrawableHeight = static_cast<float>(_drawableHeight) * 0.5F;
    const bool isOrthographic = std::abs(_projection[3][3] - 1.0F) <= std::numeric_limits<float>::epsilon();
    const float depthScale = isOrthographic ? 1.0F : std::max(std::abs(viewCenter.z), MinimumViewDepth);
    // 标准透视投影中 Projection[1][1] = cot(FovY / 2)，因此结果单位是像素
    const float pixelsPerWorldUnit = halfDrawableHeight * projectionScaleY / depthScale;
    const float heightErrorPixels = worldError * pixelsPerWorldUnit;
    // edge 项同样使用像素单位，让平坦近景仍保有足够几何密度
    const float edgeLengthPixels = longestEdgeLength * pixelsPerWorldUnit * ProjectedEdgeWeight;
    return std::max(heightErrorPixels, edgeLengthPixels);
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
