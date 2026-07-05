#include "algorithms/classic_roam/ClassicRoamMeshBuilder.h"

#include <array>

namespace ParallelRoam::Algorithms::ClassicRoam
{
void ClassicRoamMeshBuilder::EmitLeafTriangles(
    Terrain::TerrainMeshData& meshData,
    const std::vector<ClassicRoamNode*>& leafNodes) const
{
    // 当前 mesh 直接复制 active leaf 顶点，后续可加顶点缓存减少重复顶点
    for (const ClassicRoamNode* node : leafNodes)
    {
        EmitNode(*node, meshData);
    }
}

void ClassicRoamMeshBuilder::EmitNode(const ClassicRoamNode& node, Terrain::TerrainMeshData& meshData) const
{
    if (IsLeaf(&node))
    {
        EmitDomainTriangle(node, meshData);
    }
}

void ClassicRoamMeshBuilder::EmitDomainTriangle(const ClassicRoamNode& node, Terrain::TerrainMeshData& meshData) const
{
    const auto baseIndex = static_cast<std::uint32_t>(meshData.Vertices.size());
    const TriangleDomain& domain = node.Domain;
    // Classic emit 保持 append 语义，便于作为 DOD 并行 emit 的 baseline
    const std::array<glm::vec2, 3> uvs{domain.A, domain.B, domain.C};
    const glm::vec3 debugColor = DebugColorForLeaf(node);
    const float debugHighlight = DebugHighlightForLeaf(node);

    for (const glm::vec2& uv : uvs)
    {
        // ROAM leaf 顶点从 Height Map 即时采样，保证 split 后新点高度正确
        // 不缓存顶点能避免 merge/split 后共享顶点生命周期复杂化
        Terrain::TerrainMeshVertex vertex{};
        vertex.Position = DomainToWorld(uv);
        vertex.Normal = SampleNormal(uv);
        vertex.TexCoord = uv;
        vertex.Height = _heightMap->SampleBilinear(uv.x, uv.y);
        vertex.DebugColor = debugColor;
        vertex.DebugHighlight = debugHighlight;
        meshData.Vertices.push_back(vertex);
    }

    const glm::vec3 edge0 = meshData.Vertices[baseIndex + 1U].Position - meshData.Vertices[baseIndex].Position;
    const glm::vec3 edge1 = meshData.Vertices[baseIndex + 2U].Position - meshData.Vertices[baseIndex].Position;
    // world-space 绕序检查避免 UV split 顺序影响实际面朝向
    const bool pointsTowardPositiveY = glm::cross(edge0, edge1).y >= 0.0F;

    // 输出绕序统一朝向正 Y，后续开启 face culling 时不会和规则网格相反
    meshData.Indices.push_back(baseIndex);
    if (pointsTowardPositiveY)
    {
        // 当前 domain 顺序已经和规则网格方向一致
        meshData.Indices.push_back(baseIndex + 1U);
        meshData.Indices.push_back(baseIndex + 2U);
    }
    else
    {
        // 交换后两个点即可翻转三角面方向
        meshData.Indices.push_back(baseIndex + 2U);
        meshData.Indices.push_back(baseIndex + 1U);
    }
}
} // 命名空间 ParallelRoam::Algorithms::ClassicRoam
