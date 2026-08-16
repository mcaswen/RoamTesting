#pragma once

#include "algorithms/data_oriented_roam/DataOrientedRoamState.h"

namespace ParallelRoam::Algorithms::DataOrientedRoam
{
/// <summary>
/// DOD active leaf 到 CPU Mesh 的输出接口
/// 在 topology 收敛后调用；读取 state 的最终 leaf 集合，写入调用方拥有的 meshData，不改变拓扑
/// </summary>
void EmitLeafTriangles(
    DataOrientedRoamState& state,
    Terrain::TerrainMeshData& meshData,
    const std::vector<DataOrientedRoamNodeIndex>& leafNodes);
} // namespace ParallelRoam::Algorithms::DataOrientedRoam
