#include "algorithms/data_oriented_roam/DataOrientedRoamMeshBuilder.h"

#include "algorithms/ITerrainLodAlgorithm.h"
#include "algorithms/RoamNestedWedgie.h"
#include "algorithms/data_oriented_roam/DataOrientedRoamState.h"
#include "algorithms/data_oriented_roam/DataOrientedRoamThreadPool.h"
#include "tools/PerformanceTimer.h"

#include <algorithm>
#include <utility>

namespace ParallelRoam::Algorithms::DataOrientedRoam
{
namespace
{
constexpr int MaximumSupportedDepth = 20;
}

DataOrientedRoamMeshBuilder::DataOrientedRoamMeshBuilder()
    : _state(std::make_unique<DataOrientedRoamState>())
    , _threadPool(std::make_unique<DataOrientedRoamThreadPool>())
{
    _state->ThreadPool = _threadPool.get();
}

DataOrientedRoamMeshBuilder::~DataOrientedRoamMeshBuilder() = default;

DataOrientedRoamMeshBuilder::DataOrientedRoamMeshBuilder(DataOrientedRoamMeshBuilder&& other) noexcept
    : _state(std::move(other._state))
    , _threadPool(std::move(other._threadPool))
{
    if (_state != nullptr)
    {
        // state 只借用线程池指针，builder move 后必须重新绑定
        _state->ThreadPool = _threadPool.get();
    }
}

DataOrientedRoamMeshBuilder& DataOrientedRoamMeshBuilder::operator=(DataOrientedRoamMeshBuilder&& other) noexcept
{
    if (this == &other)
    {
        return *this;
    }

    _state = std::move(other._state);
    _threadPool = std::move(other._threadPool);
    if (_state != nullptr)
    {
        // 移动赋值会替换 owner，旧裸指针不能继续留在 state 中
        _state->ThreadPool = _threadPool.get();
    }

    return *this;
}

Terrain::TerrainMeshData DataOrientedRoamMeshBuilder::Build(
    const Terrain::HeightMap& heightMap,
    float terrainSize,
    float heightScale,
    const TerrainLodViewInput& view,
    const DataOrientedRoamSettings& settings)
{
    return BuildInternal(heightMap, terrainSize, heightScale, view, settings, true);
}

void DataOrientedRoamMeshBuilder::UpdateTopology(
    const Terrain::HeightMap& heightMap,
    float terrainSize,
    float heightScale,
    const TerrainLodViewInput& view,
    const DataOrientedRoamSettings& settings)
{
    (void)BuildInternal(heightMap, terrainSize, heightScale, view, settings, false);
}

Terrain::TerrainMeshData DataOrientedRoamMeshBuilder::BuildInternal(
    const Terrain::HeightMap& heightMap,
    float terrainSize,
    float heightScale,
    const TerrainLodViewInput& view,
    const DataOrientedRoamSettings& settings,
    bool emitCpuMesh)
{
    DataOrientedRoamState& state = *_state;
    Tools::PerformanceTimer updateTimer;
    ++state.BuildSequence;

    DataOrientedRoamSettings normalizedSettings = settings;
    normalizedSettings.MaxDepth = std::clamp(normalizedSettings.MaxDepth, 0, MaximumSupportedDepth);
    normalizedSettings.TriangleBudget = std::max<std::size_t>(normalizedSettings.TriangleBudget, 2U);
    const int varianceTreeDepth = Roam::ResolveNestedWedgieTreeDepth(
        heightMap.Width(),
        heightMap.Height(),
        normalizedSettings.MaxDepth,
        MaximumSupportedDepth);
    const bool resetTopology = NeedsTopologyReset(state, heightMap, terrainSize, heightScale, normalizedSettings);
    const bool rebuildVarianceTrees =
        state.VarianceHeightMap != &heightMap || state.VarianceTreeMaxDepth != varianceTreeDepth;
    state.HeightMap = &heightMap;
    state.Settings = normalizedSettings;
    // merge 阈值不能高于 split 阈值
    // 否则同一帧可能在 hysteresis 区间反复展开和回收
    state.Settings.MergeThreshold = std::min(state.Settings.MergeThreshold, state.Settings.SplitThreshold);
    state.Stats = {};
    state.CurrentSplitPaths.clear();
    state.ViewProjection = view.ViewProjection;
    state.FrustumPlanes = view.FrustumPlanes;
    state.DrawableWidth = std::max(view.DrawableWidth, 1U);
    state.DrawableHeight = std::max(view.DrawableHeight, 1U);
    state.TerrainSize = terrainSize;
    state.HeightScale = heightScale;

    Terrain::TerrainMeshData meshData{};
    meshData.GridWidth = heightMap.Width();
    meshData.GridHeight = heightMap.Height();
    meshData.TerrainSize = terrainSize;
    meshData.HeightScale = heightScale;

    if (!heightMap.IsValid())
    {
        // 保持返回空 mesh 的语义和 Classic builder 一致
        return meshData;
    }

    if (rebuildVarianceTrees)
    {
        RebuildVarianceTrees(state, varianceTreeDepth);
    }

    ReserveNodePool(state);
    if (resetTopology)
    {
        // reset 只发生在缓存误差或深度上限不再兼容时
        ResetTopology(state);
    }
    else if (rebuildVarianceTrees)
    {
        // 预计算树扩深时保留拓扑，但已有节点必须刷新 nested wedgie thickness
        RefreshNodeVarianceErrors(state);
    }
    const float prepareMilliseconds = updateTimer.ElapsedMilliseconds();

    Tools::PerformanceTimer mergeTimer;
    // merge pass 先运行，远处旧细节先回收
    MergeWithDiamondQueue(state);
    const float mergeMilliseconds = mergeTimer.Stop();

    Tools::PerformanceTimer splitTimer;
    // 持久 Q_s 并行刷新 priority 并生成提交快照，随后执行 split/crossover。
    RefineWithSplitQueue(state);
    const float splitMilliseconds = splitTimer.Stop();

    if (state.Settings.EnableTopologyValidation)
    {
        Tools::PerformanceTimer validateTimer;
        ValidateTopology(state);
        state.Stats.ValidateMilliseconds = validateTimer.Stop();
    }

    // split/merge 已增量维护 ActiveLeafNodes；拓扑稳定后直接复用这份只读输出视图，
    // 避免为了 emit、统计和 GPU snapshot 再从两个 root 递归遍历或复制活动 leaf。
    const std::vector<DataOrientedRoamNodeIndex>& finalActiveLeaves = state.ActiveLeafNodes;
    Tools::PerformanceTimer meshEmitTimer;
    if (emitCpuMesh)
    {
        EmitLeafTriangles(state, meshData, finalActiveLeaves);
    }
    const float meshEmitMilliseconds = meshEmitTimer.Stop();

    // GPU 路径不生成 CPU mesh，active triangle 数直接来自持久活动 leaf 索引。
    Tools::PerformanceTimer finalizeTimer;
    AccumulateLeafStats(state, finalActiveLeaves);
    state.Stats.PersistentSplitQueueSize = finalActiveLeaves.size();
    state.Stats.PersistentMergeQueueSize = state.MergeQueue.size();
    state.Stats.MergeMilliseconds = mergeMilliseconds;
    state.Stats.SplitMilliseconds = splitMilliseconds;
    state.Stats.EmitMilliseconds = emitCpuMesh
        ? meshEmitMilliseconds
        : 0.0F;
    state.Stats.PrepareMilliseconds = prepareMilliseconds;
    // DOD 直接用 Q_s.size() 计算预算，不再有独立的 leaf collect。
    state.Stats.BudgetLeafCollectMilliseconds = 0.0F;
    // 字段为统一报告 schema 保留；DOD 不再执行最终 leaf collect/copy pass。
    state.Stats.FinalLeafCollectMilliseconds = 0.0F;
    state.Stats.MeshEmitMilliseconds = emitCpuMesh
        ? meshEmitMilliseconds
        : 0.0F;

    CollectActiveSplitPaths(state);
    // split path 集合是 hysteresis 的跨帧状态
    // 必须在 merge 和 split 都完成后再更新
    state.PreviousSplitPaths = state.CurrentSplitPaths;
    state.TopologyMaxDepth = state.Settings.MaxDepth;
    state.Stats.FinalizeMilliseconds = finalizeTimer.Stop();
    state.Stats.UpdateMilliseconds = updateTimer.Stop();
    return meshData;
}

const DataOrientedRoamStats& DataOrientedRoamMeshBuilder::Stats() const
{
    return _state->Stats;
}

const DataOrientedRoamState& DataOrientedRoamMeshBuilder::State() const
{
    return *_state;
}
} // 命名空间 ParallelRoam::Algorithms::DataOrientedRoam
