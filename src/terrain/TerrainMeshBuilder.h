#pragma once

#include "terrain/HeightMap.h"

#include <glm/glm.hpp>

#include <cstdint>
#include <vector>

namespace ParallelRoam::Terrain
{
struct TerrainMeshVertex
{
    glm::vec3 Position{0.0F};
    glm::vec3 Normal{0.0F, 1.0F, 0.0F};
    glm::vec2 TexCoord{0.0F};
    float Height{0.0F};
    glm::vec3 DebugColor{0.28F, 0.34F, 0.30F};
    float DebugHighlight{0.35F};
};

struct TerrainMeshData
{
    std::vector<TerrainMeshVertex> Vertices;
    std::vector<std::uint32_t> Indices;
    int GridWidth{0};
    int GridHeight{0};
    float TerrainSize{0.0F};
    float HeightScale{0.0F};
};

/// <summary>
/// 将 Height Map 转换为规则网格 terrain mesh
/// </summary>
class TerrainMeshBuilder
{
public:
    /// <summary>
    /// 按给定世界尺寸和高度缩放生成顶点、索引和基础法线
    /// </summary>
    /// <param name="heightMap">已加载的高度图。</param>
    /// <param name="terrainSize">地形在 XZ 平面的世界尺寸。</param>
    /// <param name="heightScale">高度缩放。</param>
    [[nodiscard]] static TerrainMeshData Build(const HeightMap& heightMap, float terrainSize, float heightScale);

private:
    static void AccumulateNormals(TerrainMeshData& meshData);
};
} // 命名空间 ParallelRoam::Terrain
