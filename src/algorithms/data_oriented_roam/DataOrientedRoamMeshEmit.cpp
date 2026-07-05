#include "algorithms/data_oriented_roam/DataOrientedRoamState.h"

#include <array>

namespace ParallelRoam::Algorithms::DataOrientedRoam
{
namespace
{
void EmitDomainTriangle(
    const DataOrientedRoamState& state,
    const DataOrientedRoamNode& node,
    Terrain::TerrainMeshData& meshData)
{
    const auto baseIndex = static_cast<std::uint32_t>(meshData.Vertices.size());
    const TriangleDomain& domain = node.Domain;
    const std::array<glm::vec2, 3> uvs{domain.A, domain.B, domain.C};
    const glm::vec3 debugColor = DebugColorForLeaf(state, node);
    const float debugHighlight = DebugHighlightForLeaf(state, node);

    for (const glm::vec2& uv : uvs)
    {
        // leaf 顶点从 HeightMap 即时采样
        // 这样 split 后的新中点高度和规则网格 baseline 一致
        Terrain::TerrainMeshVertex vertex{};
        vertex.Position = DomainToWorld(state, uv);
        vertex.Normal = SampleNormal(state, uv);
        vertex.TexCoord = uv;
        vertex.Height = state.HeightMap->SampleBilinear(uv.x, uv.y);
        vertex.DebugColor = debugColor;
        vertex.DebugHighlight = debugHighlight;
        meshData.Vertices.push_back(vertex);
    }

    // 通过世界空间叉积判断最终渲染绕序
    const glm::vec3 edge0 = meshData.Vertices[baseIndex + 1U].Position - meshData.Vertices[baseIndex].Position;
    const glm::vec3 edge1 = meshData.Vertices[baseIndex + 2U].Position - meshData.Vertices[baseIndex].Position;
    const bool pointsTowardPositiveY = glm::cross(edge0, edge1).y >= 0.0F;

    // domain 绕序在递归 split 后可能和渲染面朝向相反
    // emit 阶段统一翻到正 Y
    meshData.Indices.push_back(baseIndex);
    if (pointsTowardPositiveY)
    {
        meshData.Indices.push_back(baseIndex + 1U);
        meshData.Indices.push_back(baseIndex + 2U);
    }
    else
    {
        meshData.Indices.push_back(baseIndex + 2U);
        meshData.Indices.push_back(baseIndex + 1U);
    }
}
} // 匿名命名空间

void EmitLeafTriangles(const DataOrientedRoamState& state, Terrain::TerrainMeshData& meshData)
{
    // 3A 仍输出 CPU mesh
    // 后续 SoA 和并行阶段会先保持这个渲染出口稳定
    std::vector<DataOrientedRoamNodeIndex> leafNodes;
    CollectLeafNodes(state, leafNodes);

    for (DataOrientedRoamNodeIndex node : leafNodes)
    {
        if (state.IsLeaf(node))
        {
            // CollectLeafNodes 已保证 active leaf，这里保留防御检查
            EmitDomainTriangle(state, state.Nodes[node], meshData);
        }
    }
}
} // 命名空间 ParallelRoam::Algorithms::DataOrientedRoam
