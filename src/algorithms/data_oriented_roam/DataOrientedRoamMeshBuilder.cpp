#include "algorithms/data_oriented_roam/DataOrientedRoamMeshBuilder.h"

#include "algorithms/ITerrainLodAlgorithm.h"
#include "algorithms/data_oriented_roam/DataOrientedRoamState.h"
#include "algorithms/data_oriented_roam/DataOrientedRoamThreadPool.h"

#include <algorithm>
#include <chrono>
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
    const auto updateStart = std::chrono::steady_clock::now();
    ++state.BuildSequence;

    DataOrientedRoamSettings normalizedSettings = settings;
    normalizedSettings.MaxDepth = std::clamp(normalizedSettings.MaxDepth, 0, MaximumSupportedDepth);
    normalizedSettings.TriangleBudget = std::max<std::size_t>(normalizedSettings.TriangleBudget, 2U);
    const bool resetTopology = NeedsTopologyReset(state, heightMap, terrainSize, heightScale, normalizedSettings);
    const bool rebuildVarianceTrees =
        state.VarianceHeightMap != &heightMap || state.VarianceTreeMaxDepth != normalizedSettings.MaxDepth;
    state.HeightMap = &heightMap;
    state.Settings = normalizedSettings;
    // merge 阈值不能高于 split 阈值
    // 否则同一帧可能在 hysteresis 区间反复展开和回收
    state.Settings.MergeThreshold = std::min(state.Settings.MergeThreshold, state.Settings.SplitThreshold);
    state.Stats = {};
    state.CurrentSplitPaths.clear();
    state.FinalActiveLeaves.clear();
    state.View = view.View;
    state.Projection = view.Projection;
    state.FrustumPlanes = view.FrustumPlanes;
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
        RebuildVarianceTrees(state);
    }

    ReserveNodePool(state);
    if (resetTopology)
    {
        // reset 只发生在缓存误差或深度上限不再兼容时
        ResetTopology(state);
    }
    else if (rebuildVarianceTrees)
    {
        // 提高深度时保留拓扑，但已有节点必须刷新子树最大误差
        RefreshNodeVarianceErrors(state);
    }
    const auto prepareEnd = std::chrono::steady_clock::now();

    const auto mergeStart = std::chrono::steady_clock::now();
    // merge pass 先运行，远处旧细节先回收
    MergeWithDiamondQueue(state);
    const auto mergeEnd = std::chrono::steady_clock::now();

    // 每次 leaf split 净增一个活动三角形，原子 token 供并行 commit 共同消费
    const auto budgetCollectStart = std::chrono::steady_clock::now();
    CollectLeafNodes(state, state.FinalActiveLeaves);
    const std::size_t remainingBudget = state.Settings.TriangleBudget > state.FinalActiveLeaves.size()
        ? state.Settings.TriangleBudget - state.FinalActiveLeaves.size()
        : 0U;
    state.RemainingSplitBudget.store(remainingBudget, std::memory_order_relaxed);
    state.FinalActiveLeaves.clear();
    const auto budgetCollectEnd = std::chrono::steady_clock::now();

    const auto splitStart = std::chrono::steady_clock::now();
    // split pass 再按当前相机重新分配细节
    RefineWithSplitQueue(state);
    const auto splitEnd = std::chrono::steady_clock::now();

    if (state.Settings.EnableTopologyValidation)
    {
        const auto validateStart = std::chrono::steady_clock::now();
        ValidateTopology(state);
        const auto validateEnd = std::chrono::steady_clock::now();
        state.Stats.ValidateMilliseconds = ElapsedMilliseconds(validateStart, validateEnd);
    }

    // 最终 leaf 快照在拓扑稳定后收集，emit 和统计复用同一份视图
    const auto finalCollectStart = std::chrono::steady_clock::now();
    CollectLeafNodes(state, state.FinalActiveLeaves);
    const auto finalCollectEnd = std::chrono::steady_clock::now();
    const auto meshEmitStart = std::chrono::steady_clock::now();
    if (emitCpuMesh)
    {
        // emit 计时包含最终快照收集，保持和旧路径的 mesh build 口径一致
        EmitLeafTriangles(state, meshData, state.FinalActiveLeaves);
    }
    const auto meshEmitEnd = std::chrono::steady_clock::now();

    // GPU 路径不生成 CPU mesh，active triangle 数必须来自 leaf 快照
    const auto finalizeStart = std::chrono::steady_clock::now();
    AccumulateLeafStats(state, state.FinalActiveLeaves);
    state.Stats.MergeMilliseconds = ElapsedMilliseconds(mergeStart, mergeEnd);
    state.Stats.SplitMilliseconds = ElapsedMilliseconds(splitStart, splitEnd);
    state.Stats.EmitMilliseconds = emitCpuMesh
        ? ElapsedMilliseconds(finalCollectStart, meshEmitEnd)
        : 0.0F;
    state.Stats.PrepareMilliseconds = ElapsedMilliseconds(updateStart, prepareEnd);
    state.Stats.BudgetLeafCollectMilliseconds =
        ElapsedMilliseconds(budgetCollectStart, budgetCollectEnd);
    state.Stats.FinalLeafCollectMilliseconds =
        ElapsedMilliseconds(finalCollectStart, finalCollectEnd);
    state.Stats.MeshEmitMilliseconds = emitCpuMesh
        ? ElapsedMilliseconds(meshEmitStart, meshEmitEnd)
        : 0.0F;

    CollectActiveSplitPaths(state);
    // split path 集合是 hysteresis 的跨帧状态
    // 必须在 merge 和 split 都完成后再更新
    state.PreviousSplitPaths = state.CurrentSplitPaths;
    state.TopologyMaxDepth = state.Settings.MaxDepth;
    state.Stats.FinalizeMilliseconds =
        ElapsedMilliseconds(finalizeStart, std::chrono::steady_clock::now());
    state.Stats.UpdateMilliseconds = ElapsedMilliseconds(updateStart, std::chrono::steady_clock::now());
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
