#pragma once

#include "algorithms/cbt_2024/CbtBisectCommit.h"
#include "terrain/HeightMap.h"
#include "terrain/TerrainMeshBuilder.h"

#include <glm/glm.hpp>

#include <array>
#include <cstdint>

namespace ParallelRoam::Algorithms::Cbt2024
{
/// <summary>
/// 单个活动 heapID 的高度图顶点与分类父位置 CPU 参考。
/// </summary>
struct CbtTerrainGeometryResult
{
    std::array<Terrain::TerrainMeshVertex, 3> Vertices{};
    glm::vec3 ParentClassificationPosition{0.0F};
    std::uint32_t BaseBisector{0U};
    bool Valid{false};
};

/// @brief 以与 GPU 几何 shader 相同的 LEB、双线性高度和四点差分语义生成顶点
[[nodiscard]] CbtTerrainGeometryResult EvaluateCbtTerrainGeometry(
    std::uint64_t heapId,
    std::uint32_t baseDepth,
    const std::array<CbtBaseControlPoint, CbtBaseControlPointCount>& baseControlPoints,
    const Terrain::HeightMap& heightMap,
    float terrainSize,
    float heightScale);
} // namespace ParallelRoam::Algorithms::Cbt2024
