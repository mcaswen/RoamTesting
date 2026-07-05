#include "algorithms/data_oriented_roam/DataOrientedRoamMeshBuilder.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <functional>
#include <limits>
#include <numeric>
#include <queue>
#include <unordered_map>

namespace ParallelRoam::Algorithms::DataOrientedRoam
{
namespace
{
constexpr std::uint64_t RootAPathId = 1ULL;
constexpr std::uint64_t RootBPathId = 1ULL << 32U;
constexpr int ExactReserveMaxDepth = 20;
constexpr std::size_t LargeDepthReserveFallback = 1'000'000U;

struct SplitEdge
{
    glm::vec2 Start{0.0F};
    glm::vec2 End{0.0F};
    glm::vec2 Apex{0.0F};
};

struct DomainEdge
{
    glm::vec2 Start{0.0F};
    glm::vec2 End{0.0F};
};

struct QuantizedPoint
{
    long long X{0};
    long long Y{0};
};

struct QuantizedLineKey
{
    long long DirectionX{0};
    long long DirectionY{0};
    long long Constant{0};

    bool operator==(const QuantizedLineKey& other) const
    {
        return DirectionX == other.DirectionX &&
               DirectionY == other.DirectionY &&
               Constant == other.Constant;
    }
};

struct QuantizedLineKeyHash
{
    std::size_t operator()(const QuantizedLineKey& key) const
    {
        std::size_t seed = 1469598103934665603ULL;
        const auto mix = [&seed](long long value) {
            const std::size_t hashedValue = std::hash<long long>{}(value);
            seed ^= hashedValue + 0x9e3779b97f4a7c15ULL + (seed << 6U) + (seed >> 2U);
        };
        mix(key.DirectionX);
        mix(key.DirectionY);
        mix(key.Constant);
        return seed;
    }
};

struct QuantizedEdge
{
    QuantizedLineKey Line;
    long long MinParameter{0};
    long long MaxParameter{0};
};

float DistanceSquared(const glm::vec2& a, const glm::vec2& b)
{
    const glm::vec2 delta = b - a;
    return glm::dot(delta, delta);
}

bool SamePoint(const glm::vec2& a, const glm::vec2& b)
{
    constexpr float Epsilon = 0.000001F;
    return DistanceSquared(a, b) <= Epsilon * Epsilon;
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
    return SplitEdge{domain.A, domain.B, domain.C};
}

bool SameUndirectedEdge(const DomainEdge& left, const DomainEdge& right)
{
    return (SamePoint(left.Start, right.Start) && SamePoint(left.End, right.End)) ||
           (SamePoint(left.Start, right.End) && SamePoint(left.End, right.Start));
}

long long AbsoluteGcd(long long a, long long b)
{
    return std::gcd(std::llabs(a), std::llabs(b));
}

QuantizedPoint QuantizePoint(const glm::vec2& point, int maxDepth)
{
    const auto scale = static_cast<long long>(1ULL << static_cast<unsigned int>(std::clamp(maxDepth, 0, 30)));
    return QuantizedPoint{
        static_cast<long long>(std::llround(static_cast<double>(point.x) * static_cast<double>(scale))),
        static_cast<long long>(std::llround(static_cast<double>(point.y) * static_cast<double>(scale))),
    };
}

QuantizedLineKey MakeLineKey(const QuantizedPoint& start, const QuantizedPoint& end)
{
    long long directionX = end.X - start.X;
    long long directionY = end.Y - start.Y;
    const long long divisor = std::max(AbsoluteGcd(directionX, directionY), 1LL);
    directionX /= divisor;
    directionY /= divisor;

    if (directionX < 0 || (directionX == 0 && directionY < 0))
    {
        directionX = -directionX;
        directionY = -directionY;
    }

    return QuantizedLineKey{
        directionX,
        directionY,
        directionY * start.X - directionX * start.Y,
    };
}

long long ProjectToLineParameter(const QuantizedPoint& point, const QuantizedLineKey& line)
{
    return line.DirectionX * point.X + line.DirectionY * point.Y;
}

std::uint64_t LeftChildPathId(std::uint64_t parentPathId)
{
    return parentPathId * 2ULL;
}

std::uint64_t RightChildPathId(std::uint64_t parentPathId)
{
    return parentPathId * 2ULL + 1ULL;
}

float ElapsedMilliseconds(std::chrono::steady_clock::time_point start, std::chrono::steady_clock::time_point end)
{
    const std::chrono::duration<float, std::milli> elapsed = end - start;
    return elapsed.count();
}

std::size_t ExactBintreeNodeCapacity(int maxDepth)
{
    const int safeDepth = std::clamp(maxDepth, 0, ExactReserveMaxDepth);
    const std::size_t nodesPerRoot = (std::size_t{1} << static_cast<unsigned int>(safeDepth + 1)) - 1U;
    return nodesPerRoot * 2U;
}
} // 匿名命名空间

Terrain::TerrainMeshData DataOrientedRoamMeshBuilder::Build(
    const Terrain::HeightMap& heightMap,
    float terrainSize,
    float heightScale,
    const glm::vec3& cameraPosition,
    const DataOrientedRoamSettings& settings)
{
    const auto updateStart = std::chrono::steady_clock::now();
    ++_buildSequence;
    const bool resetTopology = NeedsTopologyReset(heightMap, terrainSize, heightScale, settings);
    _heightMap = &heightMap;
    _settings = settings;
    _settings.MergeThreshold = std::min(_settings.MergeThreshold, _settings.SplitThreshold);
    _stats = {};
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

    ReserveNodePool();

    if (resetTopology)
    {
        ResetTopology();
    }

    const auto mergeStart = std::chrono::steady_clock::now();
    MergeWithDiamondQueue();
    const auto mergeEnd = std::chrono::steady_clock::now();

    const auto splitStart = std::chrono::steady_clock::now();
    RefineWithSplitQueue(_rootA, _rootB);
    const auto splitEnd = std::chrono::steady_clock::now();

    if (_settings.EnableTopologyValidation)
    {
        const auto validateStart = std::chrono::steady_clock::now();
        ValidateTopology();
        const auto validateEnd = std::chrono::steady_clock::now();
        _stats.ValidateMilliseconds = ElapsedMilliseconds(validateStart, validateEnd);
    }

    const auto emitStart = std::chrono::steady_clock::now();
    EmitLeafTriangles(meshData);
    const auto emitEnd = std::chrono::steady_clock::now();

    _stats.NodeCount = _nodes.size();
    _stats.ReservedNodeCapacity = _nodes.capacity();
    _stats.ActiveTriangleCount = meshData.Indices.size() / 3U;

    std::vector<NodeIndex> activeLeaves;
    CollectLeafNodes(activeLeaves);
    _stats.MaxDepthReached = 0;
    for (NodeIndex leafIndex : activeLeaves)
    {
        const DataOrientedRoamNode& leaf = _nodes[leafIndex];
        _stats.MaxDepthReached = std::max(_stats.MaxDepthReached, leaf.Depth);
        switch (ClassifyLeafDebug(leaf))
        {
        case LeafDebugClass::Original:
            ++_stats.OriginalTriangleCount;
            break;
        case LeafDebugClass::Subdivided:
            ++_stats.SubdividedTriangleCount;
            break;
        case LeafDebugClass::Rebuilt:
            ++_stats.RebuiltTriangleCount;
            break;
        }
    }

    _stats.MergeMilliseconds = ElapsedMilliseconds(mergeStart, mergeEnd);
    _stats.SplitMilliseconds = ElapsedMilliseconds(splitStart, splitEnd);
    _stats.EmitMilliseconds = ElapsedMilliseconds(emitStart, emitEnd);
    _stats.UpdateMilliseconds = ElapsedMilliseconds(updateStart, std::chrono::steady_clock::now());
    CollectActiveSplitPaths();
    _previousSplitPaths = _currentSplitPaths;
    _topologyMaxDepth = _settings.MaxDepth;
    return meshData;
}

const DataOrientedRoamStats& DataOrientedRoamMeshBuilder::Stats() const
{
    return _stats;
}

DataOrientedRoamMeshBuilder::NodeIndex DataOrientedRoamMeshBuilder::AddNode(
    const TriangleDomain& domain,
    NodeIndex parent,
    int depth,
    std::uint64_t pathId)
{
    DataOrientedRoamNode node{};
    node.Domain = domain;
    node.Parent = parent;
    node.Depth = depth;
    node.PathId = pathId;
    node.CreatedBuildId = _buildSequence;
    node.ActivatedBuildId = _buildSequence;
    node.GeometricError = ComputeGeometricError(domain);
    _stats.MaxDepthReached = std::max(_stats.MaxDepthReached, depth);

    _nodes.push_back(node);
    return static_cast<NodeIndex>(_nodes.size() - 1U);
}

void DataOrientedRoamMeshBuilder::ReserveNodePool()
{
    std::size_t targetCapacity = 2U + _settings.SplitBudget * 2U;
    if (_settings.MaxDepth <= ExactReserveMaxDepth)
    {
        targetCapacity = std::max(targetCapacity, ExactBintreeNodeCapacity(_settings.MaxDepth));
    }
    else
    {
        targetCapacity = std::max(targetCapacity, LargeDepthReserveFallback);
    }

    if (_nodes.capacity() < targetCapacity)
    {
        _nodes.reserve(targetCapacity);
    }
}

void DataOrientedRoamMeshBuilder::ResetTopology()
{
    _nodes.clear();
    _previousSplitPaths.clear();
    _currentSplitPaths.clear();

    _rootA = AddNode(
        TriangleDomain{glm::vec2{0.0F, 1.0F}, glm::vec2{1.0F, 0.0F}, glm::vec2{0.0F, 0.0F}},
        InvalidNodeIndex,
        0,
        RootAPathId);
    _rootB = AddNode(
        TriangleDomain{glm::vec2{1.0F, 0.0F}, glm::vec2{0.0F, 1.0F}, glm::vec2{1.0F, 1.0F}},
        InvalidNodeIndex,
        0,
        RootBPathId);

    _nodes[_rootA].BaseNeighbor = _rootB;
    _nodes[_rootB].BaseNeighbor = _rootA;
    _topologyMaxDepth = _settings.MaxDepth;
}

bool DataOrientedRoamMeshBuilder::NeedsTopologyReset(
    const Terrain::HeightMap& heightMap,
    float terrainSize,
    float heightScale,
    const DataOrientedRoamSettings& settings) const
{
    if (!IsValidNode(_rootA) || !IsValidNode(_rootB) || _nodes.empty())
    {
        return true;
    }

    if (_heightMap != &heightMap)
    {
        return true;
    }

    if (settings.MaxDepth < _topologyMaxDepth)
    {
        return true;
    }

    return terrainSize != _terrainSize || heightScale != _heightScale;
}

void DataOrientedRoamMeshBuilder::RefineWithSplitQueue(NodeIndex rootA, NodeIndex rootB)
{
    struct SplitCandidate
    {
        float Score{0.0F};
        std::uint64_t Sequence{0};
        NodeIndex Node{InvalidNodeIndex};
    };

    struct CandidateCompare
    {
        bool operator()(const SplitCandidate& left, const SplitCandidate& right) const
        {
            if (left.Score == right.Score)
            {
                return left.Sequence > right.Sequence;
            }

            return left.Score < right.Score;
        }
    };

    std::priority_queue<SplitCandidate, std::vector<SplitCandidate>, CandidateCompare> candidates;
    std::uint64_t sequence = 0;

    const auto enqueueCandidate = [this, &candidates, &sequence](NodeIndex node) {
        if (!IsValidNode(node) || !IsLeaf(node) || _nodes[node].Depth >= _settings.MaxDepth)
        {
            return;
        }

        const float score = ComputeScreenErrorScore(_nodes[node]);
        if (!ShouldSplitWithScore(_nodes[node], score))
        {
            return;
        }

        candidates.push(SplitCandidate{score, sequence++, node});
        _stats.CandidatePeakCount = std::max(_stats.CandidatePeakCount, candidates.size());
    };

    const auto enqueueActiveLeaves = [&enqueueCandidate, this](auto&& self, NodeIndex node) -> void {
        if (!IsValidNode(node))
        {
            return;
        }

        if (IsLeaf(node))
        {
            enqueueCandidate(node);
            return;
        }

        self(self, _nodes[node].LeftChild);
        self(self, _nodes[node].RightChild);
    };

    enqueueActiveLeaves(enqueueActiveLeaves, rootA);
    enqueueActiveLeaves(enqueueActiveLeaves, rootB);

    while (!candidates.empty())
    {
        const SplitCandidate candidate = candidates.top();
        candidates.pop();

        const NodeIndex node = candidate.Node;
        if (!IsValidNode(node) || !IsLeaf(node))
        {
            continue;
        }

        const float score = ComputeScreenErrorScore(_nodes[node]);
        if (!ShouldSplitWithScore(_nodes[node], score))
        {
            continue;
        }

        if (_settings.SplitBudget > 0U && _stats.SplitCount >= _settings.SplitBudget)
        {
            ++_stats.RejectedSplitCount;
            break;
        }

        const NodeIndex baseNeighborBeforeSplit = _nodes[node].BaseNeighbor;
        if (!SplitNode(node, SplitReason::Requested, InvalidNodeIndex))
        {
            ++_stats.RejectedSplitCount;
            continue;
        }

        enqueueCandidate(_nodes[node].LeftChild);
        enqueueCandidate(_nodes[node].RightChild);

        if (IsValidNode(baseNeighborBeforeSplit) && !IsLeaf(baseNeighborBeforeSplit))
        {
            enqueueCandidate(_nodes[baseNeighborBeforeSplit].LeftChild);
            enqueueCandidate(_nodes[baseNeighborBeforeSplit].RightChild);
        }
    }
}

void DataOrientedRoamMeshBuilder::MergeWithDiamondQueue()
{
    struct MergeCandidate
    {
        float Score{0.0F};
        NodeIndex Node{InvalidNodeIndex};
    };

    std::vector<MergeCandidate> candidates;
    const auto collectCandidates = [this, &candidates](auto&& self, NodeIndex node) -> void {
        if (!IsValidNode(node) || IsLeaf(node))
        {
            return;
        }

        if (CanMergeNode(node))
        {
            candidates.push_back(MergeCandidate{ComputeScreenErrorScore(_nodes[node]), node});
        }

        self(self, _nodes[node].LeftChild);
        self(self, _nodes[node].RightChild);
    };

    collectCandidates(collectCandidates, _rootA);
    collectCandidates(collectCandidates, _rootB);

    std::sort(
        candidates.begin(),
        candidates.end(),
        [](const MergeCandidate& left, const MergeCandidate& right) {
            return left.Score < right.Score;
        });

    for (const MergeCandidate& candidate : candidates)
    {
        if (!CanMergeNode(candidate.Node))
        {
            continue;
        }

        if (!MergeNodeOrDiamond(candidate.Node))
        {
            ++_stats.RejectedMergeCount;
        }
    }
}

bool DataOrientedRoamMeshBuilder::SplitNode(NodeIndex node, SplitReason reason, NodeIndex forcedFrom)
{
    if (!IsValidNode(node) || !IsLeaf(node))
    {
        return false;
    }

    if (_nodes[node].Depth >= _settings.MaxDepth)
    {
        ++_stats.RejectedSplitCount;
        return false;
    }

    NodeIndex baseNeighbor = _nodes[node].BaseNeighbor;
    if (_settings.EnableLocalConstraints)
    {
        int guard = 0;
        while (IsValidNode(baseNeighbor) &&
               baseNeighbor != forcedFrom &&
               _nodes[baseNeighbor].BaseNeighbor != node &&
               guard < _settings.MaxDepth + 2)
        {
            ++_stats.ConstraintPassCount;
            if (!SplitNode(baseNeighbor, SplitReason::ForcedByBaseNeighbor, node))
            {
                return false;
            }

            baseNeighbor = _nodes[node].BaseNeighbor;
            ++guard;
        }
    }

    if (_settings.EnableLocalConstraints && IsValidNode(baseNeighbor) && IsLeaf(baseNeighbor) && baseNeighbor != forcedFrom)
    {
        ++_stats.ConstraintPassCount;
        if (!SplitNode(baseNeighbor, SplitReason::ForcedByBaseNeighbor, node))
        {
            return false;
        }

        baseNeighbor = _nodes[node].BaseNeighbor;
    }

    const std::uint64_t parentPathId = _nodes[node].PathId;
    if (!IsValidNode(_nodes[node].LeftChild) || !IsValidNode(_nodes[node].RightChild))
    {
        const TriangleDomain domain = _nodes[node].Domain;
        const int childDepth = _nodes[node].Depth + 1;
        const SplitEdge edge = ChooseBaseEdge(domain);
        const glm::vec2 midpoint = (edge.Start + edge.End) * 0.5F;

        const TriangleDomain leftDomain{edge.Apex, edge.Start, midpoint};
        const TriangleDomain rightDomain{edge.End, edge.Apex, midpoint};
        const NodeIndex leftChild = AddNode(leftDomain, node, childDepth, LeftChildPathId(parentPathId));
        const NodeIndex rightChild = AddNode(rightDomain, node, childDepth, RightChildPathId(parentPathId));
        _nodes[node].LeftChild = leftChild;
        _nodes[node].RightChild = rightChild;
    }

    DataOrientedRoamNode& parent = _nodes[node];
    parent.IsSplit = true;
    parent.SplitBuildId = _buildSequence;

    DataOrientedRoamNode& leftChild = _nodes[parent.LeftChild];
    DataOrientedRoamNode& rightChild = _nodes[parent.RightChild];
    leftChild.BaseNeighbor = InvalidNodeIndex;
    leftChild.LeftNeighbor = InvalidNodeIndex;
    leftChild.RightNeighbor = InvalidNodeIndex;
    rightChild.BaseNeighbor = InvalidNodeIndex;
    rightChild.LeftNeighbor = InvalidNodeIndex;
    rightChild.RightNeighbor = InvalidNodeIndex;
    leftChild.ActivatedBuildId = _buildSequence;
    rightChild.ActivatedBuildId = _buildSequence;
    leftChild.ActivatedByForcedSplit = reason != SplitReason::Requested;
    rightChild.ActivatedByForcedSplit = reason != SplitReason::Requested;

    LinkSplitNeighbors(node, baseNeighbor);
    _currentSplitPaths.insert(parentPathId);
    ++_stats.SplitCount;
    if (reason != SplitReason::Requested)
    {
        ++_stats.ForcedSplitCount;
    }
    return true;
}

void DataOrientedRoamMeshBuilder::LinkSplitNeighbors(NodeIndex node, NodeIndex baseNeighbor)
{
    if (!IsValidNode(node))
    {
        return;
    }

    const NodeIndex leftChild = _nodes[node].LeftChild;
    const NodeIndex rightChild = _nodes[node].RightChild;
    if (!IsValidNode(leftChild) || !IsValidNode(rightChild))
    {
        return;
    }

    _nodes[leftChild].LeftNeighbor = rightChild;
    _nodes[rightChild].RightNeighbor = leftChild;

    _nodes[leftChild].BaseNeighbor = _nodes[node].LeftNeighbor;
    _nodes[rightChild].BaseNeighbor = _nodes[node].RightNeighbor;
    ReplaceNeighborReference(_nodes[node].LeftNeighbor, node, leftChild);
    ReplaceNeighborReference(_nodes[node].RightNeighbor, node, rightChild);

    if (!IsValidNode(baseNeighbor) || IsLeaf(baseNeighbor))
    {
        return;
    }

    _nodes[leftChild].RightNeighbor = _nodes[baseNeighbor].RightChild;
    _nodes[rightChild].LeftNeighbor = _nodes[baseNeighbor].LeftChild;
    if (IsValidNode(_nodes[baseNeighbor].RightChild))
    {
        _nodes[_nodes[baseNeighbor].RightChild].LeftNeighbor = leftChild;
    }

    if (IsValidNode(_nodes[baseNeighbor].LeftChild))
    {
        _nodes[_nodes[baseNeighbor].LeftChild].RightNeighbor = rightChild;
    }
}

void DataOrientedRoamMeshBuilder::ReplaceNeighborReference(NodeIndex neighbor, NodeIndex oldNode, NodeIndex newNode)
{
    if (!IsValidNode(neighbor))
    {
        return;
    }

    if (_nodes[neighbor].BaseNeighbor == oldNode)
    {
        _nodes[neighbor].BaseNeighbor = newNode;
    }

    if (_nodes[neighbor].LeftNeighbor == oldNode)
    {
        _nodes[neighbor].LeftNeighbor = newNode;
    }

    if (_nodes[neighbor].RightNeighbor == oldNode)
    {
        _nodes[neighbor].RightNeighbor = newNode;
    }
}

bool DataOrientedRoamMeshBuilder::CanMergeNode(NodeIndex node) const
{
    if (!IsValidNode(node) || IsLeaf(node))
    {
        return false;
    }

    const DataOrientedRoamNode& candidate = _nodes[node];
    if (!IsValidNode(candidate.LeftChild) || !IsValidNode(candidate.RightChild))
    {
        return false;
    }

    if (!IsLeaf(candidate.LeftChild) || !IsLeaf(candidate.RightChild))
    {
        return false;
    }

    if (ComputeScreenErrorScore(candidate) > _settings.MergeThreshold)
    {
        return false;
    }

    const NodeIndex baseNeighbor = candidate.BaseNeighbor;
    if (!IsValidNode(baseNeighbor) || IsLeaf(baseNeighbor))
    {
        return true;
    }

    if (_nodes[baseNeighbor].BaseNeighbor != node)
    {
        return false;
    }

    if (!IsValidNode(_nodes[baseNeighbor].LeftChild) || !IsValidNode(_nodes[baseNeighbor].RightChild))
    {
        return false;
    }

    if (!IsLeaf(_nodes[baseNeighbor].LeftChild) || !IsLeaf(_nodes[baseNeighbor].RightChild))
    {
        return false;
    }

    return ComputeScreenErrorScore(_nodes[baseNeighbor]) <= _settings.MergeThreshold;
}

void DataOrientedRoamMeshBuilder::MergeSingleNode(NodeIndex node)
{
    if (!IsValidNode(node) || !IsValidNode(_nodes[node].LeftChild) || !IsValidNode(_nodes[node].RightChild))
    {
        return;
    }

    const NodeIndex leftChild = _nodes[node].LeftChild;
    const NodeIndex rightChild = _nodes[node].RightChild;
    const NodeIndex newLeftNeighbor = _nodes[leftChild].BaseNeighbor;
    const NodeIndex newRightNeighbor = _nodes[rightChild].BaseNeighbor;

    ReplaceNeighborReference(newLeftNeighbor, leftChild, node);
    ReplaceNeighborReference(newRightNeighbor, rightChild, node);
    _nodes[node].LeftNeighbor = newLeftNeighbor;
    _nodes[node].RightNeighbor = newRightNeighbor;
    _nodes[node].IsSplit = false;
    _nodes[node].ActivatedBuildId = _buildSequence;
    _nodes[node].MergeBuildId = _buildSequence;
    _nodes[node].ActivatedByForcedSplit = false;
    ++_stats.MergeCount;
}

bool DataOrientedRoamMeshBuilder::MergeNodeOrDiamond(NodeIndex node)
{
    if (!CanMergeNode(node))
    {
        return false;
    }

    const NodeIndex baseNeighbor = _nodes[node].BaseNeighbor;
    if (IsValidNode(baseNeighbor) && !IsLeaf(baseNeighbor))
    {
        if (!CanMergeNode(baseNeighbor) || _nodes[baseNeighbor].BaseNeighbor != node)
        {
            return false;
        }

        _nodes[node].BaseNeighbor = baseNeighbor;
        _nodes[baseNeighbor].BaseNeighbor = node;
        MergeSingleNode(node);
        MergeSingleNode(baseNeighbor);
        _nodes[node].BaseNeighbor = baseNeighbor;
        _nodes[baseNeighbor].BaseNeighbor = node;
        return true;
    }

    MergeSingleNode(node);
    return true;
}

void DataOrientedRoamMeshBuilder::CollectLeafNodes(std::vector<NodeIndex>& leafNodes) const
{
    leafNodes.clear();
    leafNodes.reserve(_nodes.size());
    CollectLeafNodesFrom(_rootA, leafNodes);
    CollectLeafNodesFrom(_rootB, leafNodes);
}

void DataOrientedRoamMeshBuilder::CollectLeafNodesFrom(NodeIndex node, std::vector<NodeIndex>& leafNodes) const
{
    if (!IsValidNode(node))
    {
        return;
    }

    if (IsLeaf(node))
    {
        leafNodes.push_back(node);
        return;
    }

    CollectLeafNodesFrom(_nodes[node].LeftChild, leafNodes);
    CollectLeafNodesFrom(_nodes[node].RightChild, leafNodes);
}

void DataOrientedRoamMeshBuilder::CollectActiveSplitPaths()
{
    _currentSplitPaths.clear();
    _stats.ActiveSplitCount = 0;
    CollectActiveSplitPathsFrom(_rootA);
    CollectActiveSplitPathsFrom(_rootB);
}

void DataOrientedRoamMeshBuilder::CollectActiveSplitPathsFrom(NodeIndex node)
{
    if (!IsValidNode(node) || IsLeaf(node))
    {
        return;
    }

    _currentSplitPaths.insert(_nodes[node].PathId);
    ++_stats.ActiveSplitCount;
    CollectActiveSplitPathsFrom(_nodes[node].LeftChild);
    CollectActiveSplitPathsFrom(_nodes[node].RightChild);
}

void DataOrientedRoamMeshBuilder::ValidateTopology()
{
    std::vector<NodeIndex> leafNodes;
    CollectLeafNodes(leafNodes);
    std::vector<bool> leafSet(_nodes.size(), false);
    for (NodeIndex node : leafNodes)
    {
        leafSet[node] = true;
    }

    const auto validateNeighbor = [this, &leafSet](NodeIndex owner, NodeIndex neighbor, const DomainEdge& edge) {
        if (!IsValidNode(neighbor) || !leafSet[neighbor])
        {
            return false;
        }

        for (const DomainEdge& neighborEdge : DomainEdges(_nodes[neighbor].Domain))
        {
            if (SameUndirectedEdge(edge, neighborEdge))
            {
                return _nodes[neighbor].BaseNeighbor == owner ||
                       _nodes[neighbor].LeftNeighbor == owner ||
                       _nodes[neighbor].RightNeighbor == owner;
            }
        }

        return false;
    };

    std::unordered_map<QuantizedLineKey, std::vector<long long>, QuantizedLineKeyHash> lineVertices;
    lineVertices.reserve(leafNodes.size() * 3U);
    std::vector<QuantizedEdge> quantizedEdges;
    quantizedEdges.reserve(leafNodes.size() * 3U);

    for (NodeIndex node : leafNodes)
    {
        const std::array<DomainEdge, 3> edges = DomainEdges(_nodes[node].Domain);
        for (const DomainEdge& edge : edges)
        {
            const QuantizedPoint start = QuantizePoint(edge.Start, _settings.MaxDepth);
            const QuantizedPoint end = QuantizePoint(edge.End, _settings.MaxDepth);
            const QuantizedLineKey line = MakeLineKey(start, end);
            const long long startParameter = ProjectToLineParameter(start, line);
            const long long endParameter = ProjectToLineParameter(end, line);
            quantizedEdges.push_back(QuantizedEdge{
                line,
                std::min(startParameter, endParameter),
                std::max(startParameter, endParameter),
            });

            std::vector<long long>& vertexParameters = lineVertices[line];
            vertexParameters.push_back(startParameter);
            vertexParameters.push_back(endParameter);
        }
    }

    for (auto& [line, vertexParameters] : lineVertices)
    {
        (void)line;
        std::sort(vertexParameters.begin(), vertexParameters.end());
        vertexParameters.erase(std::unique(vertexParameters.begin(), vertexParameters.end()), vertexParameters.end());
    }

    for (const QuantizedEdge& edge : quantizedEdges)
    {
        const auto lineIt = lineVertices.find(edge.Line);
        if (lineIt == lineVertices.end())
        {
            continue;
        }

        const std::vector<long long>& vertexParameters = lineIt->second;
        const auto interiorIt = std::upper_bound(vertexParameters.begin(), vertexParameters.end(), edge.MinParameter);
        if (interiorIt != vertexParameters.end() && *interiorIt < edge.MaxParameter)
        {
            ++_stats.TjunctionCount;
            ++_stats.CrackRiskCount;
        }
    }

    for (NodeIndex node : leafNodes)
    {
        const std::array<DomainEdge, 3> edges = DomainEdges(_nodes[node].Domain);

        if (IsValidNode(_nodes[node].BaseNeighbor) && !validateNeighbor(node, _nodes[node].BaseNeighbor, edges[0]))
        {
            ++_stats.InvalidNeighborCount;
        }

        if (IsValidNode(_nodes[node].RightNeighbor) && !validateNeighbor(node, _nodes[node].RightNeighbor, edges[1]))
        {
            ++_stats.InvalidNeighborCount;
        }

        if (IsValidNode(_nodes[node].LeftNeighbor) && !validateNeighbor(node, _nodes[node].LeftNeighbor, edges[2]))
        {
            ++_stats.InvalidNeighborCount;
        }
    }

    if (!IsValidNode(_rootA) ||
        !IsValidNode(_rootB) ||
        _nodes[_rootA].BaseNeighbor != _rootB ||
        _nodes[_rootB].BaseNeighbor != _rootA)
    {
        ++_stats.InvalidTopologyCount;
    }

    for (NodeIndex nodeIndex = 0; nodeIndex < _nodes.size(); ++nodeIndex)
    {
        const DataOrientedRoamNode& node = _nodes[nodeIndex];
        if (node.IsSplit && (!IsValidNode(node.LeftChild) || !IsValidNode(node.RightChild)))
        {
            ++_stats.InvalidTopologyCount;
        }

        if (IsValidNode(node.LeftChild) && _nodes[node.LeftChild].Parent != nodeIndex)
        {
            ++_stats.InvalidTopologyCount;
        }

        if (IsValidNode(node.RightChild) && _nodes[node.RightChild].Parent != nodeIndex)
        {
            ++_stats.InvalidTopologyCount;
        }

        if (nodeIndex != _rootA && nodeIndex != _rootB && !IsValidNode(node.Parent))
        {
            ++_stats.InvalidTopologyCount;
        }
    }
}

void DataOrientedRoamMeshBuilder::EmitLeafTriangles(Terrain::TerrainMeshData& meshData) const
{
    std::vector<NodeIndex> leafNodes;
    CollectLeafNodes(leafNodes);

    for (NodeIndex node : leafNodes)
    {
        EmitNode(node, meshData);
    }
}

void DataOrientedRoamMeshBuilder::EmitNode(NodeIndex node, Terrain::TerrainMeshData& meshData) const
{
    if (IsLeaf(node))
    {
        EmitDomainTriangle(_nodes[node], meshData);
    }
}

void DataOrientedRoamMeshBuilder::EmitDomainTriangle(
    const DataOrientedRoamNode& node,
    Terrain::TerrainMeshData& meshData) const
{
    const auto baseIndex = static_cast<std::uint32_t>(meshData.Vertices.size());
    const TriangleDomain& domain = node.Domain;
    const std::array<glm::vec2, 3> uvs{domain.A, domain.B, domain.C};
    const glm::vec3 debugColor = DebugColorForLeaf(node);
    const float debugHighlight = DebugHighlightForLeaf(node);

    for (const glm::vec2& uv : uvs)
    {
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
    const bool pointsTowardPositiveY = glm::cross(edge0, edge1).y >= 0.0F;

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

bool DataOrientedRoamMeshBuilder::ShouldSplitWithScore(
    const DataOrientedRoamNode& node,
    float screenErrorScore) const
{
    if (node.Depth >= _settings.MaxDepth)
    {
        return false;
    }

    if (screenErrorScore > _settings.SplitThreshold)
    {
        return true;
    }

    if (screenErrorScore < _settings.MergeThreshold)
    {
        return false;
    }

    return WasSplitLastFrame(node);
}

bool DataOrientedRoamMeshBuilder::WasSplitLastFrame(const DataOrientedRoamNode& node) const
{
    return _previousSplitPaths.find(node.PathId) != _previousSplitPaths.end();
}

DataOrientedRoamMeshBuilder::LeafDebugClass DataOrientedRoamMeshBuilder::ClassifyLeafDebug(
    const DataOrientedRoamNode& node) const
{
    if (node.ActivatedBuildId == _buildSequence || node.MergeBuildId == _buildSequence)
    {
        return LeafDebugClass::Rebuilt;
    }

    if (node.Depth > 0)
    {
        return LeafDebugClass::Subdivided;
    }

    return LeafDebugClass::Original;
}

glm::vec3 DataOrientedRoamMeshBuilder::DebugColorForLeaf(const DataOrientedRoamNode& node) const
{
    const float depthRatio = std::clamp(
        static_cast<float>(node.Depth) / static_cast<float>(std::max(_settings.MaxDepth, 1)),
        0.0F,
        1.0F);

    switch (ClassifyLeafDebug(node))
    {
    case LeafDebugClass::Original:
        return glm::vec3{0.28F, 0.34F, 0.30F};
    case LeafDebugClass::Subdivided:
        return glm::mix(glm::vec3{0.08F, 0.72F, 0.62F}, glm::vec3{0.10F, 0.34F, 0.95F}, depthRatio);
    case LeafDebugClass::Rebuilt:
        if (node.ActivatedByForcedSplit)
        {
            return glm::mix(glm::vec3{0.96F, 0.34F, 0.90F}, glm::vec3{0.96F, 0.16F, 0.42F}, depthRatio);
        }

        return glm::mix(glm::vec3{1.0F, 0.68F, 0.15F}, glm::vec3{1.0F, 0.34F, 0.10F}, depthRatio);
    }

    return glm::vec3{0.28F, 0.34F, 0.30F};
}

float DataOrientedRoamMeshBuilder::DebugHighlightForLeaf(const DataOrientedRoamNode& node) const
{
    switch (ClassifyLeafDebug(node))
    {
    case LeafDebugClass::Original:
        return 0.35F;
    case LeafDebugClass::Subdivided:
        return 0.70F;
    case LeafDebugClass::Rebuilt:
        return 1.0F;
    }

    return 0.35F;
}

float DataOrientedRoamMeshBuilder::ComputeGeometricError(const TriangleDomain& domain) const
{
    const float heightA = _heightMap->SampleBilinear(domain.A.x, domain.A.y);
    const float heightB = _heightMap->SampleBilinear(domain.B.x, domain.B.y);
    const float heightC = _heightMap->SampleBilinear(domain.C.x, domain.C.y);

    const auto edgeMidpointError = [this](const glm::vec2& start, const glm::vec2& end, float startHeight, float endHeight) {
        const glm::vec2 midpoint = (start + end) * 0.5F;
        const float midpointHeight = _heightMap->SampleBilinear(midpoint.x, midpoint.y);
        const float interpolatedHeight = (startHeight + endHeight) * 0.5F;
        return std::abs(midpointHeight - interpolatedHeight);
    };

    const glm::vec2 centroid = (domain.A + domain.B + domain.C) / 3.0F;
    const float centroidHeight = _heightMap->SampleBilinear(centroid.x, centroid.y);
    const float centroidInterpolatedHeight = (heightA + heightB + heightC) / 3.0F;

    return std::max({
        edgeMidpointError(domain.A, domain.B, heightA, heightB),
        edgeMidpointError(domain.B, domain.C, heightB, heightC),
        edgeMidpointError(domain.C, domain.A, heightC, heightA),
        std::abs(centroidHeight - centroidInterpolatedHeight),
    });
}

float DataOrientedRoamMeshBuilder::ComputeScreenErrorScore(const DataOrientedRoamNode& node) const
{
    const glm::vec3 a = DomainToWorld(node.Domain.A);
    const glm::vec3 b = DomainToWorld(node.Domain.B);
    const glm::vec3 c = DomainToWorld(node.Domain.C);
    const glm::vec3 center = (a + b + c) / 3.0F;
    const float distance = std::max(glm::length(center - _cameraPosition), 0.05F);
    const float worldError = node.GeometricError * _heightScale;
    const float longestEdgeLength = std::max({
        glm::length(a - b),
        glm::length(b - c),
        glm::length(c - a),
    });
    constexpr float ProjectedEdgeWeight = 0.20F;
    const float heightErrorScore = worldError * _settings.DistanceScale / distance;
    const float edgeLengthScore = longestEdgeLength * ProjectedEdgeWeight / distance;
    return std::max(heightErrorScore, edgeLengthScore);
}

glm::vec3 DataOrientedRoamMeshBuilder::DomainToWorld(const glm::vec2& uv) const
{
    const float height = _heightMap->SampleBilinear(uv.x, uv.y);
    return glm::vec3{
        (uv.x - 0.5F) * _terrainSize,
        height * _heightScale,
        (uv.y - 0.5F) * _terrainSize,
    };
}

glm::vec3 DataOrientedRoamMeshBuilder::SampleNormal(const glm::vec2& uv) const
{
    const float stepU = 1.0F / static_cast<float>(std::max(_heightMap->Width() - 1, 1));
    const float stepV = 1.0F / static_cast<float>(std::max(_heightMap->Height() - 1, 1));
    const float left = _heightMap->SampleBilinear(uv.x - stepU, uv.y);
    const float right = _heightMap->SampleBilinear(uv.x + stepU, uv.y);
    const float down = _heightMap->SampleBilinear(uv.x, uv.y - stepV);
    const float up = _heightMap->SampleBilinear(uv.x, uv.y + stepV);

    const glm::vec3 tangentX{stepU * 2.0F * _terrainSize, (right - left) * _heightScale, 0.0F};
    const glm::vec3 tangentZ{0.0F, (up - down) * _heightScale, stepV * 2.0F * _terrainSize};
    const glm::vec3 normal = glm::cross(tangentZ, tangentX);

    if (glm::dot(normal, normal) <= std::numeric_limits<float>::epsilon())
    {
        return glm::vec3{0.0F, 1.0F, 0.0F};
    }

    return glm::normalize(normal);
}

bool DataOrientedRoamMeshBuilder::IsValidNode(NodeIndex node) const
{
    return node != InvalidNodeIndex && node < _nodes.size();
}

bool DataOrientedRoamMeshBuilder::IsLeaf(NodeIndex node) const
{
    return IsValidNode(node) && !_nodes[node].IsSplit;
}
} // 命名空间 ParallelRoam::Algorithms::DataOrientedRoam
