#include "algorithms/data_oriented_roam/DataOrientedRoamState.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <thread>
#include <vector>

namespace ParallelRoam::Algorithms::DataOrientedRoam
{
namespace
{
// 自动模式沿用其他 DOD pass 的保守线程上限
constexpr std::size_t MaxAutoEmitWorkerCount = 8;
// 小 mesh 保持串行输出，避免 thread 启动成本超过顶点写入收益
constexpr std::size_t MinParallelEmitLeafCount = 256;

std::size_t ResolveEmitWorkerCount(const DataOrientedRoamState& state, std::size_t leafCount)
{
    if (leafCount == 0U)
    {
        return 0U;
    }

    if (state.Settings.ErrorEvaluationWorkerCount == 1U || leafCount < MinParallelEmitLeafCount)
    {
        // worker 设置沿用 DOD CPU 宽度，避免新增 benchmark 参数
        return 1U;
    }

    std::size_t requestedWorkerCount = state.Settings.ErrorEvaluationWorkerCount;
    if (requestedWorkerCount == 0U)
    {
        const unsigned int hardwareWorkerCount = std::thread::hardware_concurrency();
        requestedWorkerCount = hardwareWorkerCount == 0U ? 1U : static_cast<std::size_t>(hardwareWorkerCount);
        // emit 主要受 HeightMap 采样和顶点写入限制，不无限追随硬件线程数
        requestedWorkerCount = std::min(requestedWorkerCount, MaxAutoEmitWorkerCount);
    }

    return std::clamp(requestedWorkerCount, std::size_t{1}, leafCount);
}

void WriteDomainTriangle(
    const DataOrientedRoamState& state,
    DataOrientedRoamNodeConstRef node,
    Terrain::TerrainMeshData& meshData,
    std::size_t triangleIndex)
{
    const auto baseIndex = static_cast<std::uint32_t>(triangleIndex * 3U);
    const TriangleDomain& domain = node.Domain;
    // uvs 直接引用 domain 三个顶点，emit 不创建持久顶点缓存
    const std::array<glm::vec2, 3> uvs{domain.A, domain.B, domain.C};
    // debug 属性按 leaf 计算一次，再复制到三个顶点
    const glm::vec3 debugColor = DebugColorForLeaf(state, node);
    const float debugHighlight = DebugHighlightForLeaf(state, node);

    for (std::size_t vertexOffset = 0U; vertexOffset < uvs.size(); ++vertexOffset)
    {
        // leaf 顶点从 HeightMap 即时采样
        // 这样 split 后的新中点高度和规则网格 baseline 一致
        const glm::vec2& uv = uvs[vertexOffset];
        Terrain::TerrainMeshVertex vertex{};
        vertex.Position = DomainToWorld(state, uv);
        vertex.Normal = SampleNormal(state, uv);
        vertex.TexCoord = uv;
        vertex.Height = state.HeightMap->SampleBilinear(uv.x, uv.y);
        vertex.DebugColor = debugColor;
        vertex.DebugHighlight = debugHighlight;
        const std::size_t vertexIndex = static_cast<std::size_t>(baseIndex) + vertexOffset;
        meshData.Vertices[vertexIndex] = vertex;
    }

    // 通过世界空间叉积判断最终渲染绕序
    const glm::vec3 edge0 = meshData.Vertices[baseIndex + 1U].Position - meshData.Vertices[baseIndex].Position;
    const glm::vec3 edge1 = meshData.Vertices[baseIndex + 2U].Position - meshData.Vertices[baseIndex].Position;
    const bool pointsTowardPositiveY = glm::cross(edge0, edge1).y >= 0.0F;

    // domain 绕序在递归 split 后可能和渲染面朝向相反
    // mesh 输出统一翻到正 Y
    meshData.Indices[baseIndex] = baseIndex;
    if (pointsTowardPositiveY)
    {
        meshData.Indices[baseIndex + 1U] = baseIndex + 1U;
        meshData.Indices[baseIndex + 2U] = baseIndex + 2U;
    }
    else
    {
        meshData.Indices[baseIndex + 1U] = baseIndex + 2U;
        meshData.Indices[baseIndex + 2U] = baseIndex + 1U;
    }
}

void EmitLeafRange(
    const DataOrientedRoamState& state,
    Terrain::TerrainMeshData& meshData,
    const std::vector<DataOrientedRoamNodeIndex>& leafNodes,
    std::size_t begin,
    std::size_t end)
{
    for (std::size_t index = begin; index < end; ++index)
    {
        const DataOrientedRoamNodeIndex node = leafNodes[index];
        if (state.IsLeaf(node))
        {
            // leafNodes 来自最终 active 拓扑，这里保留防御检查
            WriteDomainTriangle(state, state.Nodes[node], meshData, index);
        }
    }
}
} // 匿名命名空间

void EmitLeafTriangles(
    DataOrientedRoamState& state,
    Terrain::TerrainMeshData& meshData,
    const std::vector<DataOrientedRoamNodeIndex>& leafNodes)
{
    const std::size_t leafCount = leafNodes.size();
    state.Stats.EmitWorkerCount = ResolveEmitWorkerCount(state, leafCount);
    // 预设最终大小后，各 worker 只写独占三角形槽位
    meshData.Vertices.resize(leafCount * 3U);
    meshData.Indices.resize(leafCount * 3U);

    if (leafCount == 0U)
    {
        return;
    }

    const std::size_t workerCount = state.Stats.EmitWorkerCount;
    if (workerCount <= 1U)
    {
        // 串行路径和并行路径共用同一写入函数
        EmitLeafRange(state, meshData, leafNodes, 0U, leafCount);
        return;
    }

    std::vector<std::thread> workers;
    workers.reserve(workerCount);
    // leaf 快照的顺序就是输出 triangleIndex，分段不会改变索引稳定性
    const std::size_t chunkSize = (leafCount + workerCount - 1U) / workerCount;
    for (std::size_t workerIndex = 0U; workerIndex < workerCount; ++workerIndex)
    {
        const std::size_t begin = workerIndex * chunkSize;
        const std::size_t end = std::min(begin + chunkSize, leafCount);
        if (begin >= end)
        {
            break;
        }

        workers.emplace_back([&state, &meshData, &leafNodes, begin, end]() {
            EmitLeafRange(state, meshData, leafNodes, begin, end);
        });
    }

    for (std::thread& worker : workers)
    {
        worker.join();
    }
}
} // 命名空间 ParallelRoam::Algorithms::DataOrientedRoam
