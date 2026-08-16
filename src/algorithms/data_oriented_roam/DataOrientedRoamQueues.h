#pragma once

#include "algorithms/data_oriented_roam/DataOrientedRoamState.h"

namespace ParallelRoam::Algorithms::DataOrientedRoam
{
/// <summary>
/// DOD 的双持久优先队列接口
/// queue 容器由 DataOrientedRoamState 持有；初始化只在 reset，priority refresh 和局部成员更新发生在每帧 Build
/// queue pass 与 topology pass 会修改它；消费者是 MergeWithDiamondQueue 和 RefineWithSplitQueue
/// </summary>
void InitializePersistentMergeQueue(DataOrientedRoamState& state);
void InitializePersistentSplitQueue(DataOrientedRoamState& state);
// split queue 在当前活动叶集合上保存高误差候选
void RefreshPersistentSplitQueuePriorities(DataOrientedRoamState& state);

// 下列接口维护 split queue 的 membership、堆顶和候选快照
void InsertPersistentSplitQueueNode(
    DataOrientedRoamState& state,
    DataOrientedRoamNodeIndex node);
void RemovePersistentSplitQueueNode(
    DataOrientedRoamState& state,
    DataOrientedRoamNodeIndex node);
void BlockPersistentSplitQueueNodeForCurrentBuild(
    DataOrientedRoamState& state,
    DataOrientedRoamNodeIndex node);
[[nodiscard]] DataOrientedRoamNodeIndex TopPersistentSplitQueueNode(
    const DataOrientedRoamState& state);
[[nodiscard]] float TopPersistentSplitQueueScore(const DataOrientedRoamState& state);
void SnapshotPersistentSplitQueueCandidates(
    const DataOrientedRoamState& state,
    std::vector<DataOrientedRoamSplitCandidate>& candidates);

// merge queue 每个 diamond 只保留一个代表节点；topology 修改后只刷新受影响邻域
void RefreshPersistentMergeQueuePriorities(DataOrientedRoamState& state);
void AppendPersistentMergeQueueNeighborhood(
    const DataOrientedRoamState& state,
    DataOrientedRoamNodeIndex node,
    std::vector<DataOrientedRoamNodeIndex>& nodes);
void InvalidatePersistentMergeQueueNeighborhood(
    DataOrientedRoamState& state,
    const std::vector<DataOrientedRoamNodeIndex>& nodes);
void RefreshPersistentMergeQueueNeighborhood(
    DataOrientedRoamState& state,
    const std::vector<DataOrientedRoamNodeIndex>& nodes);
void RemovePersistentMergeQueueCandidate(
    DataOrientedRoamState& state,
    DataOrientedRoamNodeIndex node);
[[nodiscard]] DataOrientedRoamNodeIndex TopPersistentMergeQueueNode(
    const DataOrientedRoamState& state);
[[nodiscard]] float TopPersistentMergeQueueScore(const DataOrientedRoamState& state);
void SnapshotPersistentMergeQueueCandidates(
    const DataOrientedRoamState& state,
    float maximumScore,
    std::vector<DataOrientedRoamMergeCandidate>& candidates);
[[nodiscard]] std::size_t CountPersistentQueueInvariantViolations(
    const DataOrientedRoamState& state);
} // namespace ParallelRoam::Algorithms::DataOrientedRoam
