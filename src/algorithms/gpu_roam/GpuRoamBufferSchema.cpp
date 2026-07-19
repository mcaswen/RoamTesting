#include "algorithms/gpu_roam/GpuRoamBufferSchema.h"

#include <algorithm>

namespace ParallelRoam::Algorithms::GpuRoam
{
namespace
{
// GLSL 4.30 不依赖原生 uint64 字段，所有构建序列号拆成两个 uint
std::uint32_t Low32(std::uint64_t value)
{
    return static_cast<std::uint32_t>(value & 0xFFFFFFFFULL);
}

std::uint32_t High32(std::uint64_t value)
{
    return static_cast<std::uint32_t>(value >> 32U);
}
} // namespace

std::size_t GpuRoamBufferSnapshot::NodeBufferBytes() const
{
    // 以共享 NodeRecord 大小计算，不能使用 CPU SoA 单字段容量
    return Nodes.size() * sizeof(GpuRoamNodeRecord);
}

std::size_t GpuRoamBufferSnapshot::ActiveLeafBufferBytes() const
{
    // 活动列表保存物理节点索引而不是三角形顶点索引
    return ActiveLeafIndices.size() * sizeof(std::uint32_t);
}

GpuRoamBufferSnapshot BuildGpuRoamBufferSnapshot(
    const DataOrientedRoam::DataOrientedRoamState& state)
{
    // 快照冻结一次 CPU topology build，所有 GPU pass 必须消费同一 BuildSequence
    GpuRoamBufferSnapshot snapshot{};
    snapshot.BuildSequence = state.BuildSequence;
    snapshot.MaxDepth = state.Settings.MaxDepth;
    snapshot.MaxDepthReached = state.Stats.MaxDepthReached;

    // 保留完整节点池而不仅是 active leaf，使 GPU split 能访问 parent 和 neighbor
    const std::size_t nodeCount = state.Nodes.size();
    snapshot.Nodes.resize(nodeCount);
    snapshot.ActiveLeafIndices.reserve(state.FinalActiveLeaves.size());

    // 位标记和稠密列表同时生成，前者进入 NodeRecord，后者用于首轮并行调度
    std::vector<std::uint8_t> activeLeafFlags(nodeCount, 0U);
    for (DataOrientedRoam::DataOrientedRoamNodeIndex leaf : state.FinalActiveLeaves)
    {
        // 防御无效索引，损坏的 CPU leaf 不允许传播到 shader 地址计算
        if (leaf != DataOrientedRoam::InvalidDataOrientedRoamNodeIndex &&
            static_cast<std::size_t>(leaf) < nodeCount)
        {
            activeLeafFlags[leaf] = 1U;
            snapshot.ActiveLeafIndices.push_back(static_cast<std::uint32_t>(leaf));
        }
    }

    // CPU SoA 在这里转为 std430 AoS，字段顺序受 static_assert 和 HLSL/GLSL 共同约束
    for (DataOrientedRoam::DataOrientedRoamNodeIndex node = 0; node < nodeCount; ++node)
    {
        const DataOrientedRoam::DataOrientedRoamNodeConstRef source = state.Nodes[node];
        GpuRoamNodeRecord& target = snapshot.Nodes[node];

        // domain 顶点按 A.xy B.xy C.xy 排列，shader 据此重建三角形世界位置
        target.DomainAAndB[0] = source.Domain.A.x;
        target.DomainAAndB[1] = source.Domain.A.y;
        target.DomainAAndB[2] = source.Domain.B.x;
        target.DomainAAndB[3] = source.Domain.B.y;

        target.DomainCAndErrors[0] = source.Domain.C.x;
        target.DomainCAndErrors[1] = source.Domain.C.y;
        target.DomainCAndErrors[2] = source.GeometricError;
        target.DomainCAndErrors[3] = source.ScreenError;

        // topology0 的四个索引是 split pass 访问亲缘和 base-neighbor 的核心契约
        target.Topology0[0] = source.Parent;
        target.Topology0[1] = source.LeftChild;
        target.Topology0[2] = source.RightChild;
        target.Topology0[3] = source.BaseNeighbor;

        // flags 只描述当前快照状态，不修改 CPU 节点自身持久字段
        std::uint32_t flags = 0U;
        if (source.IsSplit != 0U)
        {
            flags |= GpuRoamNodeFlagIsSplit;
        }
        if (source.ActivatedByForcedSplit != 0U)
        {
            flags |= GpuRoamNodeFlagActivatedByForcedSplit;
        }
        if (activeLeafFlags[node] != 0U)
        {
            flags |= GpuRoamNodeFlagActiveLeaf;
        }

        // 左右邻接和 chunk id 为后续兼容性与并发实验保留
        target.Topology1[0] = source.LeftNeighbor;
        target.Topology1[1] = source.RightNeighbor;
        target.Topology1[2] = source.InteriorChunkId;
        target.Topology1[3] = flags;

        // 64 位 path/build id 拆分后低位在前，与 shader buildIdMatches 组合顺序一致
        target.PathAndCreatedBuild[0] = Low32(source.PathId);
        target.PathAndCreatedBuild[1] = High32(source.PathId);
        target.PathAndCreatedBuild[2] = Low32(source.CreatedBuildId);
        target.PathAndCreatedBuild[3] = High32(source.CreatedBuildId);

        target.ActivatedAndSplitBuild[0] = Low32(source.ActivatedBuildId);
        target.ActivatedAndSplitBuild[1] = High32(source.ActivatedBuildId);
        target.ActivatedAndSplitBuild[2] = Low32(source.SplitBuildId);
        target.ActivatedAndSplitBuild[3] = High32(source.SplitBuildId);

        // depth 使用非负 uint，非法负深度在 CPU 边界收敛为零
        target.MergeBuildAndDepth[0] = Low32(source.MergeBuildId);
        target.MergeBuildAndDepth[1] = High32(source.MergeBuildId);
        target.MergeBuildAndDepth[2] = static_cast<std::uint32_t>(std::max(source.Depth, 0));
        target.MergeBuildAndDepth[3] = 0U;
    }

    // 返回后 snapshot 不再引用 DataOrientedRoamState，可安全跨上传阶段持有
    return snapshot;
}
} // namespace ParallelRoam::Algorithms::GpuRoam
