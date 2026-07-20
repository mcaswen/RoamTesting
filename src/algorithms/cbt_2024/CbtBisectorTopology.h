#pragma once

#include "algorithms/cbt_2024/CbtOccupancyTree.h"

#include <array>
#include <cstdint>
#include <limits>
#include <string>

namespace ParallelRoam::Algorithms::Cbt2024
{
inline constexpr std::uint32_t InvalidCbtBisectorIndex = std::numeric_limits<std::uint32_t>::max();
inline constexpr std::uint32_t CbtBaseBisectorCount = 6U;
inline constexpr std::uint32_t CbtBaseControlPointCount = CbtBaseBisectorCount * 3U;

/// <summary>
/// CBT 二分器按 prev、next、twin 顺序保存的物理槽位邻接
/// </summary>
struct CbtBisectorNeighbors
{
    std::uint32_t Previous{InvalidCbtBisectorIndex};
    std::uint32_t Next{InvalidCbtBisectorIndex};
    std::uint32_t Twin{InvalidCbtBisectorIndex};
};

/// <summary>
/// 基础二分器在单位高度图上的三维控制点
/// </summary>
struct CbtBaseControlPoint
{
    float U{0.0F};
    float Height{0.0F};
    float V{0.0F};
};

/// <summary>
/// 与官方 HLSL BisectorData 保持 32 字节一致的拓扑更新记录
/// </summary>
struct CbtBisectorData
{
    std::uint32_t SubdivisionPattern{0U};
    std::array<std::uint32_t, 3> Indices{};
    std::uint32_t ProblematicNeighbor{0U};
    std::uint32_t BisectorState{0U};
    std::uint32_t Flags{0U};
    std::uint32_t PropagationId{0U};
};

/// <summary>
/// 与 D3D12_DRAW_ARGUMENTS 二进制兼容的跨平台绘制参数
/// </summary>
struct CbtDrawArguments
{
    std::uint32_t VertexCountPerInstance{0U};
    std::uint32_t InstanceCount{0U};
    std::uint32_t StartVertexLocation{0U};
    std::uint32_t StartInstanceLocation{0U};
};

/// <summary>
/// 与 D3D12_DISPATCH_ARGUMENTS 二进制兼容的跨平台调度参数
/// </summary>
struct CbtDispatchArguments
{
    std::uint32_t ThreadGroupCountX{0U};
    std::uint32_t ThreadGroupCountY{0U};
    std::uint32_t ThreadGroupCountZ{0U};
};

/// <summary>
/// 一档 CBT 容量下全部拓扑、任务、索引和命令缓冲的元素布局
/// </summary>
struct CbtTopologyBufferLayout
{
    CbtOccupancyLayout Occupancy{};
    std::uint32_t DynamicElementCount{0U};
    std::uint32_t BaseElementOffset{0U};
    std::uint32_t TotalElementCount{0U};
    std::uint32_t ClassificationElementCount{0U};
    std::uint32_t SimplificationElementCount{0U};
    std::uint32_t AllocationElementCount{0U};
    std::uint32_t PropagationElementCount{0U};
    std::uint32_t IndexElementCount{0U};
    std::uint32_t DrawCommandCount{0U};
    std::uint32_t DispatchCommandCount{0U};
};

/// <summary>
/// 方形高度图的六个基础二分器及其 GPU 初始数据
/// </summary>
struct CbtBaseTopology
{
    CbtTopologyBufferLayout Layout{};
    std::uint32_t BaseDepth{0U};
    std::array<std::uint64_t, CbtBaseBisectorCount> HeapIds{};
    std::array<CbtBisectorNeighbors, CbtBaseBisectorCount> Neighbors{};
    std::array<CbtBaseControlPoint, CbtBaseControlPointCount> ControlPoints{};
    std::array<std::uint32_t, CbtBaseBisectorCount> ActiveIndices{};
    std::array<std::uint32_t, CbtBaseBisectorCount> VisibleIndices{};
    std::array<CbtDrawArguments, 3> DrawCommands{};
    std::array<CbtDispatchArguments, 3> DispatchCommands{};
};

[[nodiscard]] CbtTopologyBufferLayout BuildCbtTopologyBufferLayout(CbtOccupancyCapacity capacity);
[[nodiscard]] CbtBaseTopology BuildSquareCbtBaseTopology(CbtOccupancyCapacity capacity);

/// @brief 返回 false 时 errorMessage 给出首个违反的拓扑或布局约束
[[nodiscard]] bool ValidateCbtBaseTopology(const CbtBaseTopology& topology, std::string* errorMessage);

[[nodiscard]] std::uint32_t CbtHeapIdDepth(std::uint64_t heapId);
[[nodiscard]] std::uint64_t CbtHeapIdParent(std::uint64_t heapId);
[[nodiscard]] std::uint64_t CbtHeapIdChild(std::uint64_t heapId, bool rightChild);
} // namespace ParallelRoam::Algorithms::Cbt2024
