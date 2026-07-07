#include "algorithms/data_oriented_roam/DataOrientedRoamState.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace ParallelRoam::Algorithms::DataOrientedRoam
{
namespace
{
constexpr float MinimumCameraDistance = 0.05F;
constexpr float ProjectedEdgeWeight = 0.20F;
constexpr float DefaultDistanceScale = 24.0F;
constexpr float NearDistanceRadiusMultiplier = 2.0F;

float ComputeNearDistanceBoost(float distance, float distanceScale)
{
    const float safeDistanceScale = std::max(distanceScale, 0.0F);
    const float nearDistanceWeight = (safeDistanceScale * NearDistanceRadiusMultiplier) / distance;
    return std::max(1.0F, std::sqrt(nearDistanceWeight));
}
} // namespace

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

float ComputeGeometricError(const DataOrientedRoamState& state, const TriangleDomain& domain)
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

float ComputeScreenErrorScore(const DataOrientedRoamState& state, DataOrientedRoamNodeConstRef node)
{
    // 高度误差负责山体起伏，边长项保证近处平坦区域仍能细分
    const glm::vec3 a = DomainToWorld(state, node.Domain.A);
    const glm::vec3 b = DomainToWorld(state, node.Domain.B);
    const glm::vec3 c = DomainToWorld(state, node.Domain.C);
    const glm::vec3 center = (a + b + c) / 3.0F;
    // distance 下限避免相机贴近三角形中心时分数爆炸
    const float distance = std::max(glm::length(center - state.CameraPosition), MinimumCameraDistance);
    const float worldError = node.GeometricError * state.HeightScale;
    // longestEdgeLength 让近处平坦区域也能继续细分
    const float longestEdgeLength = std::max({
        glm::length(a - b),
        glm::length(b - c),
        glm::length(c - a),
    });
    const float distanceScale = std::max(state.Settings.DistanceScale, 0.0F);
    // 近场额外提升让相机距离变化能更明显地反映到细分层级上
    const float nearDistanceBoost = ComputeNearDistanceBoost(distance, distanceScale);
    const float heightErrorScore = worldError * distanceScale / distance * nearDistanceBoost;
    const float edgeLengthScore =
        longestEdgeLength * ProjectedEdgeWeight * (distanceScale / DefaultDistanceScale) / distance * nearDistanceBoost;
    // 两项取最大值，避免高频地形和近处平地互相掩盖
    return std::max(heightErrorScore, edgeLengthScore);
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
