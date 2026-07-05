#include "algorithms/classic_roam/ClassicRoamMeshBuilder.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <memory>

namespace ParallelRoam::Algorithms::ClassicRoam
{
namespace
{
constexpr std::uint64_t RootAPathId = 1ULL;

// 第二棵根树放到高位区间，避免 rootA child path 与 rootB root 撞号
constexpr std::uint64_t RootBPathId = 1ULL << 32U;

// repair pass 理论上会随最大深度收敛，额外上限用于防止坏拓扑死循环
constexpr int MaxCrackRepairPassCount = 64;

struct SplitEdge
{
    // Start 和 End 是本次 split 的边，Apex 是该边对面的顶点
    glm::vec2 Start{0.0F};
    glm::vec2 End{0.0F};
    glm::vec2 Apex{0.0F};
};

struct DomainEdge
{
    // DomainEdge 用 UV 空间表达 leaf 边界
    glm::vec2 Start{0.0F};
    glm::vec2 End{0.0F};
};

float DistanceSquared(const glm::vec2& a, const glm::vec2& b)
{
    const glm::vec2 delta = b - a;
    return glm::dot(delta, delta);
}

float Cross2D(const glm::vec2& a, const glm::vec2& b)
{
    // 二维叉积用于判断点是否偏离边所在直线
    return a.x * b.y - a.y * b.x;
}

bool SamePoint(const glm::vec2& a, const glm::vec2& b)
{
    // UV 细分会产生浮点中点，需要容差判断端点重合
    constexpr float Epsilon = 0.000001F;
    return DistanceSquared(a, b) <= Epsilon * Epsilon;
}

bool IsPointStrictlyInsideSegment(const glm::vec2& point, const DomainEdge& edge)
{
    // 被切开的粗边会出现其他 leaf 顶点严格落在线段内部
    constexpr float Epsilon = 0.000001F;
    const glm::vec2 edgeVector = edge.End - edge.Start;
    const glm::vec2 pointVector = point - edge.Start;
    const float edgeLengthSquared = glm::dot(edgeVector, edgeVector);

    if (edgeLengthSquared <= Epsilon)
    {
        return false;
    }

    if (std::abs(Cross2D(edgeVector, pointVector)) > Epsilon)
    {
        return false;
    }

    const float projection = glm::dot(pointVector, edgeVector);
    return projection > Epsilon && projection < edgeLengthSquared - Epsilon;
}

std::array<glm::vec2, 3> DomainVertices(const TriangleDomain& domain)
{
    return {domain.A, domain.B, domain.C};
}

std::array<DomainEdge, 3> DomainEdges(const TriangleDomain& domain)
{
    return {
        DomainEdge{domain.A, domain.B},
        DomainEdge{domain.B, domain.C},
        DomainEdge{domain.C, domain.A},
    };
}

SplitEdge ChooseBaseEdge(const TriangleDomain& domain)
{
    // Classic ROAM 固定沿 base edge split，A/B 是 base 两端
    return SplitEdge{domain.A, domain.B, domain.C};
}

bool SameUndirectedEdge(const DomainEdge& left, const DomainEdge& right)
{
    // leaf neighbor 重建只匹配完整共享边，不把 T-junction 当作合法邻接
    // 两个方向都要匹配，因为相邻三角形通常以反向顺序保存共享边
    return (SamePoint(left.Start, right.Start) && SamePoint(left.End, right.End)) ||
           (SamePoint(left.Start, right.End) && SamePoint(left.End, right.Start));
}

std::uint64_t LeftChildPathId(std::uint64_t parentPathId)
{
    // child path 保持二叉堆编码，便于跨帧 hysteresis 复用
    return parentPathId * 2ULL;
}

std::uint64_t RightChildPathId(std::uint64_t parentPathId)
{
    // right child 通过末位 1 与 left child 区分
    return parentPathId * 2ULL + 1ULL;
}
} // 匿名命名空间

Terrain::TerrainMeshData ClassicRoamMeshBuilder::Build(
    const Terrain::HeightMap& heightMap,
    float terrainSize,
    float heightScale,
    const glm::vec3& cameraPosition,
    const ClassicRoamSettings& settings)
{
    _heightMap = &heightMap;
    _settings = settings;
    _settings.MergeThreshold = std::min(_settings.MergeThreshold, _settings.SplitThreshold);
    _stats = {};
    _nodes.clear();
    _currentSplitPaths.clear();
    _cameraPosition = cameraPosition;
    _terrainSize = terrainSize;
    _heightScale = heightScale;

    Terrain::TerrainMeshData meshData{};
    meshData.GridWidth = heightMap.Width();
    meshData.GridHeight = heightMap.Height();
    meshData.TerrainSize = terrainSize;
    meshData.HeightScale = heightScale;

    if (!heightMap.IsValid())
    {
        return meshData;
    }

    // 两个根三角形共享对角线 base edge，构成 Classic ROAM 的根 diamond
    ClassicRoamNode* rootA = AddNode(
        TriangleDomain{glm::vec2{0.0F, 1.0F}, glm::vec2{1.0F, 0.0F}, glm::vec2{0.0F, 0.0F}},
        nullptr,
        0,
        RootAPathId);
    ClassicRoamNode* rootB = AddNode(
        TriangleDomain{glm::vec2{1.0F, 0.0F}, glm::vec2{0.0F, 1.0F}, glm::vec2{1.0F, 1.0F}},
        nullptr,
        0,
        RootBPathId);
    // 根节点跨共享 base edge 互为 base neighbor
    rootA->BaseNeighbor = rootB;
    rootB->BaseNeighbor = rootA;

    // 每次 build 都从根节点重新细分，便于阶段 2 验证正确性
    RefineNode(rootA);
    RefineNode(rootB);

    if (_settings.EnableCrackFix)
    {
        // normal split 后仍可能出现多小边贴一大边，需要闭环 repair
        RepairCracksWithDiamondSplits();
    }

    // active leaf 的 neighbor 指针最终按实际共享边重建，便于调试和后续可视化
    RebuildLeafNeighborLinks();
    EmitLeafTriangles(meshData);

    _stats.NodeCount = _nodes.size();
    _stats.ActiveTriangleCount = meshData.Indices.size() / 3U;
    for (const std::uint64_t previousPath : _previousSplitPaths)
    {
        // 上一帧 split 路径在本帧消失即可视为 merge
        if (_currentSplitPaths.find(previousPath) == _currentSplitPaths.end())
        {
            ++_stats.MergeCount;
        }
    }
    _previousSplitPaths = _currentSplitPaths;
    return meshData;
}

const ClassicRoamStats& ClassicRoamMeshBuilder::Stats() const
{
    return _stats;
}

ClassicRoamMeshBuilder::ClassicRoamNode* ClassicRoamMeshBuilder::AddNode(
    const TriangleDomain& domain,
    ClassicRoamNode* parent,
    int depth,
    std::uint64_t pathId)
{
    std::unique_ptr<ClassicRoamNode> node = std::make_unique<ClassicRoamNode>();
    node->Domain = domain;
    node->Parent = parent;
    node->Depth = depth;
    node->PathId = pathId;
    node->GeometricError = ComputeGeometricError(domain);
    _stats.MaxDepthReached = std::max(_stats.MaxDepthReached, depth);

    // unique_ptr 池负责生命周期，节点之间保留 Classic ROAM 裸指针关系
    ClassicRoamNode* nodePointer = node.get();
    _nodes.push_back(std::move(node));
    return nodePointer;
}

void ClassicRoamMeshBuilder::RefineNode(ClassicRoamNode* node)
{
    if (node == nullptr)
    {
        return;
    }

    if (!IsLeaf(node))
    {
        RefineNode(node->LeftChild);
        RefineNode(node->RightChild);
        return;
    }

    if (!ShouldSplit(*node))
    {
        return;
    }

    if (!SplitNode(node, SplitReason::Requested, nullptr))
    {
        return;
    }

    RefineNode(node->LeftChild);
    RefineNode(node->RightChild);
}

bool ClassicRoamMeshBuilder::SplitNode(
    ClassicRoamNode* node,
    SplitReason reason,
    ClassicRoamNode* forcedFrom)
{
    if (!IsLeaf(node))
    {
        return false;
    }

    if (node->Depth >= _settings.MaxDepth)
    {
        return false;
    }

    ClassicRoamNode* baseNeighbor = node->BaseNeighbor;
    if (_settings.EnableCrackFix && baseNeighbor != nullptr && IsLeaf(baseNeighbor) && baseNeighbor != forcedFrom)
    {
        // Classic ROAM 先补齐 base neighbor，保证旧 base edge 两侧一起 split 成 diamond
        // forcedFrom 防止互为 base neighbor 的两个 leaf 递归回跳
        ++_stats.ConstraintPassCount;
        if (!SplitNode(baseNeighbor, SplitReason::ForcedByBaseNeighbor, node))
        {
            return false;
        }
    }

    const TriangleDomain domain = node->Domain;
    const int childDepth = node->Depth + 1;
    const std::uint64_t parentPathId = node->PathId;
    const SplitEdge edge = ChooseBaseEdge(domain);
    const glm::vec2 midpoint = (edge.Start + edge.End) * 0.5F;

    // 子节点继续把 A/B 作为 base edge，保留经典 bintree 递归语义
    const TriangleDomain leftDomain{edge.Apex, edge.Start, midpoint};
    const TriangleDomain rightDomain{edge.End, edge.Apex, midpoint};
    ClassicRoamNode* leftChild = AddNode(leftDomain, node, childDepth, LeftChildPathId(parentPathId));
    ClassicRoamNode* rightChild = AddNode(rightDomain, node, childDepth, RightChildPathId(parentPathId));
    // child 指针直接挂回父节点，体现 Classic ROAM 的对象式树结构
    node->LeftChild = leftChild;
    node->RightChild = rightChild;

    LinkSplitNeighbors(node, baseNeighbor);
    _currentSplitPaths.insert(parentPathId);
    ++_stats.SplitCount;
    if (reason != SplitReason::Requested)
    {
        ++_stats.ForcedSplitCount;
    }
    return true;
}

void ClassicRoamMeshBuilder::LinkSplitNeighbors(ClassicRoamNode* node, ClassicRoamNode* baseNeighbor)
{
    ClassicRoamNode* leftChild = node->LeftChild;
    ClassicRoamNode* rightChild = node->RightChild;
    if (leftChild == nullptr || rightChild == nullptr)
    {
        return;
    }

    // left child 的 left edge 与 right child 的 right edge 共享 split 中线
    leftChild->LeftNeighbor = rightChild;
    rightChild->RightNeighbor = leftChild;

    // child 的 base edge 分别来自父节点的 left edge 和 right edge
    leftChild->BaseNeighbor = node->LeftNeighbor;
    rightChild->BaseNeighbor = node->RightNeighbor;
    ReplaceNeighborReference(node->LeftNeighbor, node, leftChild);
    ReplaceNeighborReference(node->RightNeighbor, node, rightChild);

    if (baseNeighbor == nullptr || IsLeaf(baseNeighbor))
    {
        return;
    }

    // baseNeighbor 已经 split 时，四个 child 共同组成无裂缝 diamond
    leftChild->RightNeighbor = baseNeighbor->RightChild;
    rightChild->LeftNeighbor = baseNeighbor->LeftChild;
    if (baseNeighbor->RightChild != nullptr)
    {
        baseNeighbor->RightChild->LeftNeighbor = leftChild;
    }

    if (baseNeighbor->LeftChild != nullptr)
    {
        baseNeighbor->LeftChild->RightNeighbor = rightChild;
    }
}

void ClassicRoamMeshBuilder::ReplaceNeighborReference(
    ClassicRoamNode* neighbor,
    ClassicRoamNode* oldNode,
    ClassicRoamNode* newNode) const
{
    if (neighbor == nullptr)
    {
        return;
    }

    // 相邻 leaf 仍指向旧节点时，把它改到 split 后共享完整边的 child
    if (neighbor->BaseNeighbor == oldNode)
    {
        neighbor->BaseNeighbor = newNode;
    }

    if (neighbor->LeftNeighbor == oldNode)
    {
        neighbor->LeftNeighbor = newNode;
    }

    if (neighbor->RightNeighbor == oldNode)
    {
        neighbor->RightNeighbor = newNode;
    }
}

void ClassicRoamMeshBuilder::RepairCracksWithDiamondSplits()
{
    for (int pass = 0; pass < MaxCrackRepairPassCount; ++pass)
    {
        // 每轮先重建 leaf 邻接，让后续 SplitNode 能继续走 diamond 传播
        RebuildLeafNeighborLinks();

        std::vector<ClassicRoamNode*> forcedSplitNodes;
        const std::size_t crackRiskCount = FindCrackRepairCandidates(forcedSplitNodes);
        if (forcedSplitNodes.empty())
        {
            // 没有 T-junction 候选时，当前 active mesh 已经无裂缝
            _stats.CrackRiskCount = crackRiskCount;
            return;
        }

        ++_stats.ConstraintPassCount;
        std::sort(
            forcedSplitNodes.begin(),
            forcedSplitNodes.end(),
            [](const ClassicRoamNode* left, const ClassicRoamNode* right) {
                // 先 split 浅层 coarse leaf，能更快消除一大边贴多小边
                if (left->Depth != right->Depth)
                {
                    return left->Depth < right->Depth;
                }

                return left->PathId < right->PathId;
            });
        forcedSplitNodes.erase(std::unique(forcedSplitNodes.begin(), forcedSplitNodes.end()), forcedSplitNodes.end());

        bool splitAnyNode = false;
        for (ClassicRoamNode* node : forcedSplitNodes)
        {
            // 候选可能在本轮被 diamond 传播提前 split，需要再次确认 leaf 状态
            // SplitNode 内部会继续处理 baseNeighbor diamond 约束
            splitAnyNode = SplitNode(node, SplitReason::ForcedByCrackRepair, nullptr) || splitAnyNode;
        }

        if (!splitAnyNode)
        {
            // 所有候选都无法继续 split，通常表示已达 MaxDepth
            _stats.CrackRiskCount = crackRiskCount + forcedSplitNodes.size();
            return;
        }
    }

    std::vector<ClassicRoamNode*> remainingCandidates;
    _stats.CrackRiskCount = FindCrackRepairCandidates(remainingCandidates);
}

std::size_t ClassicRoamMeshBuilder::FindCrackRepairCandidates(std::vector<ClassicRoamNode*>& forcedSplitNodes) const
{
    std::vector<ClassicRoamNode*> leafNodes;
    CollectLeafNodes(leafNodes);

    std::size_t crackRiskCount = 0;
    for (ClassicRoamNode* coarseNode : leafNodes)
    {
        // coarseNode 的任意边被其他 leaf 顶点切开，都需要继续 split
        const std::array<DomainEdge, 3> coarseEdges = DomainEdges(coarseNode->Domain);
        bool needsForcedSplit = false;

        for (const DomainEdge& coarseEdge : coarseEdges)
        {
            for (ClassicRoamNode* otherNode : leafNodes)
            {
                if (otherNode == coarseNode)
                {
                    continue;
                }

                for (const glm::vec2& otherVertex : DomainVertices(otherNode->Domain))
                {
                    // 端点重合是合法共享点，不属于 T-junction
                    if (SamePoint(otherVertex, coarseEdge.Start) || SamePoint(otherVertex, coarseEdge.End))
                    {
                        continue;
                    }

                    if (IsPointStrictlyInsideSegment(otherVertex, coarseEdge))
                    {
                        needsForcedSplit = true;
                        break;
                    }
                }

                if (needsForcedSplit)
                {
                    break;
                }
            }

            if (needsForcedSplit)
            {
                break;
            }
        }

        if (!needsForcedSplit)
        {
            continue;
        }

        if (coarseNode->Depth >= _settings.MaxDepth)
        {
            // 深度上限挡住 repair 时保留风险统计给 UI 观察
            ++crackRiskCount;
        }
        else
        {
            forcedSplitNodes.push_back(coarseNode);
        }
    }

    return crackRiskCount;
}

void ClassicRoamMeshBuilder::CollectLeafNodes(std::vector<ClassicRoamNode*>& leafNodes) const
{
    // leaf 集合是当前 active mesh 的拓扑基础
    leafNodes.clear();
    leafNodes.reserve(_nodes.size());

    for (const std::unique_ptr<ClassicRoamNode>& node : _nodes)
    {
        // 只有 leaf 会被写入最终 mesh
        if (IsLeaf(node.get()))
        {
            leafNodes.push_back(node.get());
        }
    }
}

void ClassicRoamMeshBuilder::RebuildLeafNeighborLinks()
{
    std::vector<ClassicRoamNode*> leafNodes;
    CollectLeafNodes(leafNodes);

    for (ClassicRoamNode* node : leafNodes)
    {
        // leaf neighbor 会按最终 active mesh 重建，内部节点保留 split 时建立的经典拓扑
        node->BaseNeighbor = nullptr;
        node->LeftNeighbor = nullptr;
        node->RightNeighbor = nullptr;
    }

    const auto findNeighbor = [&leafNodes](const ClassicRoamNode* owner, const DomainEdge& edge) -> ClassicRoamNode* {
        for (ClassicRoamNode* candidate : leafNodes)
        {
            // owner 自己的边不能和自己建立邻接
            if (candidate == owner)
            {
                continue;
            }

            for (const DomainEdge& candidateEdge : DomainEdges(candidate->Domain))
            {
                // 只要任一边完整重合，就说明两个 leaf 在该边相邻
                if (SameUndirectedEdge(edge, candidateEdge))
                {
                    return candidate;
                }
            }
        }

        return nullptr;
    };

    for (ClassicRoamNode* node : leafNodes)
    {
        const std::array<DomainEdge, 3> edges = DomainEdges(node->Domain);

        // Domain edge 顺序是 base、right、left，写回经典 neighbor 指针
        // base neighbor 用于后续 diamond split 的主要约束传播
        node->BaseNeighbor = findNeighbor(node, edges[0]);
        // right neighbor 和 left neighbor 主要服务调试显示与完整邻接验证
        node->RightNeighbor = findNeighbor(node, edges[1]);
        node->LeftNeighbor = findNeighbor(node, edges[2]);
    }
}

void ClassicRoamMeshBuilder::EmitLeafTriangles(Terrain::TerrainMeshData& meshData) const
{
    // 当前 mesh 直接复制 leaf 顶点，后续可加顶点缓存减少重复顶点
    for (const std::unique_ptr<ClassicRoamNode>& node : _nodes)
    {
        EmitNode(*node, meshData);
    }
}

void ClassicRoamMeshBuilder::EmitNode(const ClassicRoamNode& node, Terrain::TerrainMeshData& meshData) const
{
    if (IsLeaf(&node))
    {
        EmitDomainTriangle(node.Domain, meshData);
    }
}

void ClassicRoamMeshBuilder::EmitDomainTriangle(const TriangleDomain& domain, Terrain::TerrainMeshData& meshData) const
{
    const auto baseIndex = static_cast<std::uint32_t>(meshData.Vertices.size());
    const std::array<glm::vec2, 3> uvs{domain.A, domain.B, domain.C};

    for (const glm::vec2& uv : uvs)
    {
        // ROAM leaf 顶点从 Height Map 即时采样，保证 split 后新点高度正确
        Terrain::TerrainMeshVertex vertex{};
        vertex.Position = DomainToWorld(uv);
        vertex.Normal = SampleNormal(uv);
        vertex.TexCoord = uv;
        vertex.Height = _heightMap->SampleBilinear(uv.x, uv.y);
        meshData.Vertices.push_back(vertex);
    }

    const glm::vec3 edge0 = meshData.Vertices[baseIndex + 1U].Position - meshData.Vertices[baseIndex].Position;
    const glm::vec3 edge1 = meshData.Vertices[baseIndex + 2U].Position - meshData.Vertices[baseIndex].Position;
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

bool ClassicRoamMeshBuilder::ShouldSplit(const ClassicRoamNode& node) const
{
    // 最大深度限制优先于误差判断，避免相机贴近时无限细分
    if (node.Depth >= _settings.MaxDepth)
    {
        return false;
    }

    const float screenErrorScore = ComputeScreenErrorScore(node);
    if (screenErrorScore > _settings.SplitThreshold)
    {
        return true;
    }

    if (screenErrorScore < _settings.MergeThreshold)
    {
        return false;
    }

    // hysteresis 区间沿用上一帧 split 状态，降低 split/merge 抖动
    return WasSplitLastFrame(node);
}

bool ClassicRoamMeshBuilder::WasSplitLastFrame(const ClassicRoamNode& node) const
{
    return _previousSplitPaths.find(node.PathId) != _previousSplitPaths.end();
}

float ClassicRoamMeshBuilder::ComputeGeometricError(const TriangleDomain& domain) const
{
    const float heightA = _heightMap->SampleBilinear(domain.A.x, domain.A.y);
    const float heightB = _heightMap->SampleBilinear(domain.B.x, domain.B.y);
    const float heightC = _heightMap->SampleBilinear(domain.C.x, domain.C.y);

    // 边中点误差能捕获边界起伏，避免只看 base edge
    const auto edgeMidpointError = [this](const glm::vec2& start, const glm::vec2& end, float startHeight, float endHeight) {
        const glm::vec2 midpoint = (start + end) * 0.5F;
        const float midpointHeight = _heightMap->SampleBilinear(midpoint.x, midpoint.y);
        const float interpolatedHeight = (startHeight + endHeight) * 0.5F;
        return std::abs(midpointHeight - interpolatedHeight);
    };

    const glm::vec2 centroid = (domain.A + domain.B + domain.C) / 3.0F;
    const float centroidHeight = _heightMap->SampleBilinear(centroid.x, centroid.y);
    const float centroidInterpolatedHeight = (heightA + heightB + heightC) / 3.0F;

    // 采样三条边中点和重心，避免非 base 边或三角形内部起伏被漏掉
    return std::max({
        edgeMidpointError(domain.A, domain.B, heightA, heightB),
        edgeMidpointError(domain.B, domain.C, heightB, heightC),
        edgeMidpointError(domain.C, domain.A, heightC, heightA),
        std::abs(centroidHeight - centroidInterpolatedHeight),
    });
}

float ClassicRoamMeshBuilder::ComputeScreenErrorScore(const ClassicRoamNode& node) const
{
    const glm::vec3 a = DomainToWorld(node.Domain.A);
    const glm::vec3 b = DomainToWorld(node.Domain.B);
    const glm::vec3 c = DomainToWorld(node.Domain.C);
    // 使用三角形中心估算视距，足够支撑阶段 2 的 LOD 展示
    const glm::vec3 center = (a + b + c) / 3.0F;
    const float distance = std::max(glm::length(center - _cameraPosition), 0.001F);
    const float worldError = node.GeometricError * _heightScale;

    // 这里用简化 screen-space error，阶段 2 先验证近细远粗的 split 行为
    return worldError * _settings.DistanceScale / distance;
}

glm::vec3 ClassicRoamMeshBuilder::DomainToWorld(const glm::vec2& uv) const
{
    // 世界空间仍以地形中心为原点，方便复用阶段 1 相机和光照
    const float height = _heightMap->SampleBilinear(uv.x, uv.y);
    return glm::vec3{
        (uv.x - 0.5F) * _terrainSize,
        height * _heightScale,
        (uv.y - 0.5F) * _terrainSize,
    };
}

glm::vec3 ClassicRoamMeshBuilder::SampleNormal(const glm::vec2& uv) const
{
    // 法线从 Height Map 梯度估计，不依赖相邻 leaf 拓扑
    const float stepU = 1.0F / static_cast<float>(std::max(_heightMap->Width() - 1, 1));
    const float stepV = 1.0F / static_cast<float>(std::max(_heightMap->Height() - 1, 1));
    const float left = _heightMap->SampleBilinear(uv.x - stepU, uv.y);
    const float right = _heightMap->SampleBilinear(uv.x + stepU, uv.y);
    const float down = _heightMap->SampleBilinear(uv.x, uv.y - stepV);
    const float up = _heightMap->SampleBilinear(uv.x, uv.y + stepV);

    const glm::vec3 tangentX{stepU * 2.0F * _terrainSize, (right - left) * _heightScale, 0.0F};
    const glm::vec3 tangentZ{0.0F, (up - down) * _heightScale, stepV * 2.0F * _terrainSize};
    const glm::vec3 normal = glm::cross(tangentZ, tangentX);

    // 极端退化时回退到竖直法线，避免 shader 中出现 NaN
    if (glm::dot(normal, normal) <= std::numeric_limits<float>::epsilon())
    {
        return glm::vec3{0.0F, 1.0F, 0.0F};
    }

    return glm::normalize(normal);
}

bool ClassicRoamMeshBuilder::IsLeaf(const ClassicRoamNode* node) const
{
    if (node == nullptr)
    {
        return false;
    }

    return node->LeftChild == nullptr && node->RightChild == nullptr;
}
} // 命名空间 ParallelRoam::Algorithms::ClassicRoam
