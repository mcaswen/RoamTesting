#include "algorithms/classic_roam/ClassicRoamMeshBuilder.h"

#include "algorithms/RoamGeometry.h"
#include "algorithms/RoamDebugVisualization.h"
#include "algorithms/RoamNestedWedgie.h"
#include "algorithms/RoamScreenError.h"
#include "algorithms/ITerrainLodAlgorithm.h"

#include <algorithm>

namespace ParallelRoam::Algorithms::ClassicRoam
{
TriangleDomainChildren SplitTriangleDomain(const TriangleDomain& domain)
{
    // A/B 始终是 base edge，C 是 apex；两个 child 继续保持逆时针绕序
    const auto children = Roam::SplitTriangleDomain(domain);
    return TriangleDomainChildren{
        children.Left,
        children.Right,
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
    // 事件字段覆盖直接事务与邻接传播；后写入的事务决定最终可见颜色。
    if (node.DebugTopologyEvent == 2U)
    {
        return LeafDebugClass::Merge;
    }

    if (node.DebugTopologyEvent == 1U)
    {
        return LeafDebugClass::Split;
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
        return Roam::OriginalDebugColor();
    case LeafDebugClass::Subdivided:
        return Roam::SubdividedDebugColor(depthRatio);
    case LeafDebugClass::Split:
        // requested 与 forced split 都属于本次展开，统一用红色表达方向。
        return Roam::SplitDebugColor();
    case LeafDebugClass::Merge:
        return Roam::MergeDebugColor();
    }

    return Roam::OriginalDebugColor();
}

float ClassicRoamMeshBuilder::DebugHighlightForLeaf(const ClassicRoamNode& node) const
{
    // highlight 与 color 分类保持同源，避免 UI debug 语义分裂
    switch (ClassifyLeafDebug(node))
    {
    case LeafDebugClass::Original:
        return Roam::OriginalDebugHighlight();
    case LeafDebugClass::Subdivided:
        return Roam::SubdividedDebugHighlight();
    case LeafDebugClass::Split:
    case LeafDebugClass::Merge:
        return Roam::EventDebugHighlight();
    }

    return Roam::OriginalDebugHighlight();
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
        return Roam::ComputeBaseMidpointDisplacement(*_heightMap, domain);
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
    const float worldError = node.GeometricError * _heightScale;
    const std::array<glm::vec3, 3U> triangle{
        Roam::DomainToWorld(*_heightMap, node.Domain.A, _terrainSize, _heightScale),
        Roam::DomainToWorld(*_heightMap, node.Domain.B, _terrainSize, _heightScale),
        Roam::DomainToWorld(*_heightMap, node.Domain.C, _terrainSize, _heightScale),
    };
    const std::size_t nearPlaneIndex = static_cast<std::size_t>(TerrainLodFrustumPlane::Near);
    return Roam::ComputeScreenErrorScore({
        triangle,
        worldError,
        _viewProjection,
        _frustumPlanes[nearPlaneIndex],
        _frustumPlanes,
        _drawableWidth,
        _drawableHeight,
    });
}
} // 命名空间 ParallelRoam::Algorithms::ClassicRoam
