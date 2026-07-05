#include "algorithms/data_oriented_roam/DataOrientedRoamState.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <numeric>
#include <unordered_map>

namespace ParallelRoam::Algorithms::DataOrientedRoam
{
namespace
{
struct DomainEdge
{
    // validator 用 UV 边界做几何邻接判断
    glm::vec2 Start{0.0F};
    glm::vec2 End{0.0F};
};

struct QuantizedPoint
{
    // 量化点把浮点中点落回 maxDepth 网格
    long long X{0};
    long long Y{0};
};

struct QuantizedLineKey
{
    // Direction 描述归一化直线方向
    long long DirectionX{0};
    long long DirectionY{0};
    // Constant 区分平行线的位置
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
    // Line 聚合同一直线上的 leaf 边
    QuantizedLineKey Line;
    // 参数区间用于判断端点是否落在粗边内部
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
    // UV 中点由浮点计算产生，端点比较需要容差
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

bool SameUndirectedEdge(const DomainEdge& left, const DomainEdge& right)
{
    // 邻接三角形通常以反向绕序保存共享边
    return (SamePoint(left.Start, right.Start) && SamePoint(left.End, right.End)) ||
           (SamePoint(left.Start, right.End) && SamePoint(left.End, right.Start));
}

long long AbsoluteGcd(long long a, long long b)
{
    return std::gcd(std::llabs(a), std::llabs(b));
}

QuantizedPoint QuantizePoint(const glm::vec2& point, int maxDepth)
{
    // clamp 到 30 避免左移溢出 long long 的安全区间
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

    // 方向归一到统一半平面，同一条无向直线才能得到相同 key
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

bool ValidateNeighbor(
    const DataOrientedRoamState& state,
    const std::vector<bool>& leafSet,
    DataOrientedRoamNodeIndex owner,
    DataOrientedRoamNodeIndex neighbor,
    const DomainEdge& edge)
{
    if (!state.IsValidNode(neighbor) || !leafSet[neighbor])
    {
        // 非 active leaf 不能作为当前帧合法 neighbor
        return false;
    }

    for (const DomainEdge& neighborEdge : DomainEdges(state.Nodes[neighbor].Domain))
    {
        if (SameUndirectedEdge(edge, neighborEdge))
        {
            return state.Nodes[neighbor].BaseNeighbor == owner ||
                   state.Nodes[neighbor].LeftNeighbor == owner ||
                   state.Nodes[neighbor].RightNeighbor == owner;
        }
    }

    return false;
}
} // 匿名命名空间

void ValidateTopology(DataOrientedRoamState& state)
{
    // validator 不修复拓扑，只把裂缝风险和邻接错误写入统计
    std::vector<DataOrientedRoamNodeIndex> leafNodes;
    CollectLeafNodes(state, leafNodes);
    std::vector<bool> leafSet(state.Nodes.size(), false);
    // leafSet 的大小直接来自 SoA 长度，覆盖 inactive child 的下标空间
    for (DataOrientedRoamNodeIndex node : leafNodes)
    {
        // leafSet 让 neighbor 验证不用反复线性查找 active leaf
        leafSet[node] = true;
    }

    std::unordered_map<QuantizedLineKey, std::vector<long long>, QuantizedLineKeyHash> lineVertices;
    lineVertices.reserve(leafNodes.size() * 3U);
    std::vector<QuantizedEdge> quantizedEdges;
    // 每个 leaf 最多贡献三条边
    quantizedEdges.reserve(leafNodes.size() * 3U);

    for (DataOrientedRoamNodeIndex node : leafNodes)
    {
        // 每条 leaf 边都记录到量化直线索引中
        const std::array<DomainEdge, 3> edges = DomainEdges(state.Nodes[node].Domain);
        for (const DomainEdge& edge : edges)
        {
            const QuantizedPoint start = QuantizePoint(edge.Start, state.Settings.MaxDepth);
            const QuantizedPoint end = QuantizePoint(edge.End, state.Settings.MaxDepth);
            const QuantizedLineKey line = MakeLineKey(start, end);
            const long long startParameter = ProjectToLineParameter(start, line);
            const long long endParameter = ProjectToLineParameter(end, line);
            // 边保存为同一直线上的一维参数区间
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
        // 排序去重后才能可靠做 interior 查询
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
        // interiorIt 排除边端点，只捕获粗边内部的细分顶点
        if (interiorIt != vertexParameters.end() && *interiorIt < edge.MaxParameter)
        {
            // 粗边内部存在其他 leaf 端点，这是典型 T-junction 风险
            ++state.Stats.TjunctionCount;
            ++state.Stats.CrackRiskCount;
        }
    }

    for (DataOrientedRoamNodeIndex node : leafNodes)
    {
        const std::array<DomainEdge, 3> edges = DomainEdges(state.Nodes[node].Domain);
        if (state.IsValidNode(state.Nodes[node].BaseNeighbor) &&
            !ValidateNeighbor(state, leafSet, node, state.Nodes[node].BaseNeighbor, edges[0]))
        {
            // base edge 的 neighbor 关系优先暴露 diamond 约束错误
            ++state.Stats.InvalidNeighborCount;
        }

        if (state.IsValidNode(state.Nodes[node].RightNeighbor) &&
            !ValidateNeighbor(state, leafSet, node, state.Nodes[node].RightNeighbor, edges[1]))
        {
            ++state.Stats.InvalidNeighborCount;
        }

        if (state.IsValidNode(state.Nodes[node].LeftNeighbor) &&
            !ValidateNeighbor(state, leafSet, node, state.Nodes[node].LeftNeighbor, edges[2]))
        {
            ++state.Stats.InvalidNeighborCount;
        }
    }

    if (!state.IsValidNode(state.RootA) ||
        !state.IsValidNode(state.RootB) ||
        state.Nodes[state.RootA].BaseNeighbor != state.RootB ||
        state.Nodes[state.RootB].BaseNeighbor != state.RootA)
    {
        // 根 diamond 失效通常意味着 split/merge 改写了不该改的 base neighbor
        ++state.Stats.InvalidTopologyCount;
    }

    for (DataOrientedRoamNodeIndex nodeIndex = 0; nodeIndex < state.Nodes.size(); ++nodeIndex)
    {
        const DataOrientedRoamNodeConstRef node = state.Nodes[nodeIndex];
        if (node.IsSplit && (!state.IsValidNode(node.LeftChild) || !state.IsValidNode(node.RightChild)))
        {
            ++state.Stats.InvalidTopologyCount;
        }

        if (state.IsValidNode(node.LeftChild) && state.Nodes[node.LeftChild].Parent != nodeIndex)
        {
            ++state.Stats.InvalidTopologyCount;
        }

        if (state.IsValidNode(node.RightChild) && state.Nodes[node.RightChild].Parent != nodeIndex)
        {
            ++state.Stats.InvalidTopologyCount;
        }

        if (nodeIndex != state.RootA && nodeIndex != state.RootB && !state.IsValidNode(node.Parent))
        {
            // 除 root 外的节点必须能回溯到 parent
            ++state.Stats.InvalidTopologyCount;
        }
    }
}
} // 命名空间 ParallelRoam::Algorithms::DataOrientedRoam
