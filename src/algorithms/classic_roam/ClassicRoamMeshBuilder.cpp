#include "algorithms/classic_roam/ClassicRoamMeshBuilder.h"

#include <algorithm>
#include <chrono>

namespace ParallelRoam::Algorithms::ClassicRoam
{
namespace
{
float ElapsedMilliseconds(std::chrono::steady_clock::time_point start, std::chrono::steady_clock::time_point end)
{
    const std::chrono::duration<float, std::milli> elapsed = end - start;
    return elapsed.count();
}
} // 匿名命名空间

Terrain::TerrainMeshData ClassicRoamMeshBuilder::Build(
    const Terrain::HeightMap& heightMap,
    float terrainSize,
    float heightScale,
    const glm::vec3& cameraPosition,
    const ClassicRoamSettings& settings)
{
    const auto updateStart = std::chrono::steady_clock::now();
    ++_buildSequence;
    // reset 判定必须在写入本帧输入前完成
    const bool resetTopology = NeedsTopologyReset(heightMap, terrainSize, heightScale, settings);
    _heightMap = &heightMap;
    _settings = settings;
    // 持久化 bintree 只在输入不兼容时重置
    // 普通相机移动复用旧 child 和 geometric error
    // merge 阈值不能高于 split 阈值，否则同一帧可能反复 split / merge
    _settings.MergeThreshold = std::min(_settings.MergeThreshold, _settings.SplitThreshold);
    _stats = {};
    _currentSplitPaths.clear();
    _activeLeaves.clear();
    _cameraPosition = cameraPosition;
    _terrainSize = terrainSize;
    _heightScale = heightScale;

    Terrain::TerrainMeshData meshData{};
    meshData.GridWidth = heightMap.Width();
    meshData.GridHeight = heightMap.Height();
    // mesh metadata 直接透传给 renderer 和 benchmark CSV
    meshData.TerrainSize = terrainSize;
    meshData.HeightScale = heightScale;

    if (!heightMap.IsValid())
    {
        // 与规则网格 builder 保持空 mesh 失败语义
        return meshData;
    }

    if (resetTopology)
    {
        // 高度图或最大深度不兼容时才清空拓扑，普通相机移动保留树结构
        ResetTopology();
    }

    const auto mergeStart = std::chrono::steady_clock::now();
    // merge 先运行，避免刚 split 的 child 在同一帧被低阈值立即回收
    MergeWithDiamondQueue();
    const auto mergeEnd = std::chrono::steady_clock::now();

    const auto splitStart = std::chrono::steady_clock::now();
    // 候选队列驱动 split，避免纯递归一次展开过多节点
    RefineWithSplitQueue(_rootA, _rootB);
    const auto splitEnd = std::chrono::steady_clock::now();

    if (_settings.EnableTopologyValidation)
    {
        // validator 是调试路径，不参与默认修复逻辑
        const auto validateStart = std::chrono::steady_clock::now();
        ValidateTopology();
        const auto validateEnd = std::chrono::steady_clock::now();
        _stats.ValidateMilliseconds = ElapsedMilliseconds(validateStart, validateEnd);
    }

    const auto emitStart = std::chrono::steady_clock::now();
    // 最终 leaf 快照在拓扑稳定后收集，emit 和统计共用
    CollectLeafNodes(_activeLeaves);
    EmitLeafTriangles(meshData, _activeLeaves);
    const auto emitEnd = std::chrono::steady_clock::now();

    AccumulateLeafStats(meshData, _activeLeaves);
    _stats.MergeMilliseconds = ElapsedMilliseconds(mergeStart, mergeEnd);
    _stats.SplitMilliseconds = ElapsedMilliseconds(splitStart, splitEnd);
    _stats.EmitMilliseconds = ElapsedMilliseconds(emitStart, emitEnd);
    // update 时间覆盖完整算法入口，便于和 wallMs 做 sanity check
    _stats.UpdateMilliseconds = ElapsedMilliseconds(updateStart, std::chrono::steady_clock::now());

    CollectActiveSplitPaths();
    // hysteresis 只使用 merge 和 split 完成后的最终 active topology
    _previousSplitPaths = _currentSplitPaths;
    _topologyMaxDepth = _settings.MaxDepth;
    return meshData;
}
} // 命名空间 ParallelRoam::Algorithms::ClassicRoam
