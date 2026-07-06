#include "algorithms/data_oriented_roam/DataOrientedRoamMeshBuilder.h"

#include "algorithms/data_oriented_roam/DataOrientedRoamState.h"
#include "algorithms/data_oriented_roam/DataOrientedRoamThreadPool.h"

#include <algorithm>
#include <chrono>
#include <utility>

namespace ParallelRoam::Algorithms::DataOrientedRoam
{
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
    const glm::vec3& cameraPosition,
    const DataOrientedRoamSettings& settings)
{
    DataOrientedRoamState& state = *_state;
    const auto updateStart = std::chrono::steady_clock::now();
    ++state.BuildSequence;

    const bool resetTopology = NeedsTopologyReset(state, heightMap, terrainSize, heightScale, settings);
    state.HeightMap = &heightMap;
    state.Settings = settings;
    // merge 阈值不能高于 split 阈值
    // 否则同一帧可能在 hysteresis 区间反复展开和回收
    state.Settings.MergeThreshold = std::min(state.Settings.MergeThreshold, state.Settings.SplitThreshold);
    state.Stats = {};
    state.CurrentSplitPaths.clear();
    state.FinalActiveLeaves.clear();
    state.CameraPosition = cameraPosition;
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

    ReserveNodePool(state);
    if (resetTopology)
    {
        // reset 只发生在缓存误差或深度上限不再兼容时
        ResetTopology(state);
    }

    const auto mergeStart = std::chrono::steady_clock::now();
    // merge pass 先运行，远处旧细节先回收
    MergeWithDiamondQueue(state);
    const auto mergeEnd = std::chrono::steady_clock::now();

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

    const auto emitStart = std::chrono::steady_clock::now();
    // 最终 leaf 快照在拓扑稳定后收集，emit 和统计复用同一份视图
    CollectLeafNodes(state, state.FinalActiveLeaves);
    // emit 计时包含最终快照收集，保持和旧路径的 mesh build 口径一致
    EmitLeafTriangles(state, meshData, state.FinalActiveLeaves);
    const auto emitEnd = std::chrono::steady_clock::now();

    // stats 聚合放在 emit 后，确保 active triangle 数来自实际输出
    AccumulateLeafStats(state, meshData, state.FinalActiveLeaves);
    state.Stats.MergeMilliseconds = ElapsedMilliseconds(mergeStart, mergeEnd);
    state.Stats.SplitMilliseconds = ElapsedMilliseconds(splitStart, splitEnd);
    state.Stats.EmitMilliseconds = ElapsedMilliseconds(emitStart, emitEnd);
    state.Stats.UpdateMilliseconds = ElapsedMilliseconds(updateStart, std::chrono::steady_clock::now());

    CollectActiveSplitPaths(state);
    // split path 集合是 hysteresis 的跨帧状态
    // 必须在 merge 和 split 都完成后再更新
    state.PreviousSplitPaths = state.CurrentSplitPaths;
    state.TopologyMaxDepth = state.Settings.MaxDepth;
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
