#pragma once

#include "algorithms/cbt_2024/CbtBisectorTopology.h"
#include "algorithms/cbt_2024/CbtTerrainGeometry.h"
#include "render/D3D12GraphicsBackend.h"

#include <d3d12.h>
#include <wrl/client.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

namespace ParallelRoam::Algorithms::Cbt2024::D3D12
{
struct D3D12CbtDiagnosticExpectation
{
    // 首帧精确对照由 CPU 分类器和 split planner 生成。
    // 后续帧不构造该参考，避免把验证逻辑带入常规热路径。
    std::uint32_t SplitCandidateCount{0U};
    std::uint32_t SimplifyCandidateCount{0U};
    // 六个基础二分器各自记录期望的四模板位图。
    std::array<std::uint32_t, CbtBaseBisectorCount> SubdivisionPatterns{};
    // 以下三项验证规划节点、实际分配槽位与剩余预算是否闭合。
    std::uint32_t PlannedSplitNodeCount{0U};
    std::uint32_t AllocatedSplitSlotCount{0U};
    std::uint32_t RemainingDynamicSlotCount{0U};
    // 初始事务的六个 retained 基础槽用于高度图顶点精确对照。
    std::array<CbtTerrainGeometryResult, CbtBaseBisectorCount> BaseGeometry{};
};

struct D3D12CbtDiagnosticSnapshot
{
    // SampleGeneration 标识延迟回读对应的 GPU 拓扑事务，而非当前 CPU 帧。
    std::uint64_t SampleGeneration{0U};
    // 分类和规划计数来自四个独立 UAV 的稳定头部。
    std::uint32_t SplitCandidateCount{0U};
    std::uint32_t SimplifyCandidateCount{0U};
    std::uint32_t PlannedSplitNodeCount{0U};
    std::uint32_t AllocatedSplitSlotCount{0U};
    std::uint32_t RemainingDynamicSlotCount{0U};
    // 下列统计由 validation UAV 在 split、commit 和传播阶段逐项累计。
    std::uint32_t DuplicateSplitClaimCount{0U};
    std::uint32_t SharedCompatibilityCount{0U};
    std::uint32_t CompatibilityStepCount{0U};
    std::uint32_t MaximumCompatibilityLength{0U};
    std::uint32_t CommittedDynamicSlotCount{0U};
    std::uint32_t SplitPropagationCount{0U};
    // 模板顺序固定为 center、right、left、triple。
    std::array<std::uint32_t, 4> BisectTemplateCounts{};
    // F merge 统计分别记录合法任务、释放槽、传播和两/四节点提交。
    // A prepared group owns exactly one retained logical parent.
    std::uint32_t PreparedSimplificationCount{0U};
    // Released slots must equal pair groups plus twice the quad groups.
    std::uint32_t ReleasedDynamicSlotCount{0U};
    // Propagation count measures deleted-sibling references that required repair.
    std::uint32_t SimplifyPropagationCount{0U};
    // Pair and quad counters partition every accepted simplification group.
    std::uint32_t PairMergeCount{0U};
    std::uint32_t QuadMergeCount{0U};
    // OCBT 根只含动态槽位；draw state 的活动数还包含六个基础二分器。
    std::uint32_t ActiveDynamicSlotCount{0U};
    std::uint32_t IndexedActiveCount{CbtBaseBisectorCount};
};

/// Owns delayed readback slots, their CPU reference data, and the persistent fault latch.
/// The frame pipeline only schedules copies and hands completed frame slots to this object.
class D3D12CbtDiagnostics
{
public:
    // staging 采用紧凑分段布局，所有偏移集中在诊断所有者中定义。
    // 这使帧调度器只负责 CopyBufferRegion，不解释回读内容。
    static constexpr std::size_t ClassificationCounterOffset = 0U;
    // allocation 头紧随两个 classification 原子计数。
    static constexpr std::size_t AllocationCounterOffset = sizeof(std::uint32_t) * 2U;
    // memory 保存已分配槽数和提交后剩余预算。
    static constexpr std::size_t MemoryCounterOffset = sizeof(std::uint32_t) * 3U;
    // validation 保留 split、merge、传播和帧前活动数的原子诊断。
    static constexpr std::size_t ValidationCounterOffset = sizeof(std::uint32_t) * 5U;
    // OCBT 根只在完整验证路径复制。
    static constexpr std::size_t OccupancyRootOffset =
        ValidationCounterOffset + sizeof(std::uint32_t) * CbtValidationWordCount;
    // draw state 位于根计数之后，保持结构体自然字节布局。
    static constexpr std::size_t DrawStateReadbackOffset =
        OccupancyRootOffset + sizeof(std::uint32_t);
    // 六个基础节点数据放在 staging 尾部，供首帧精确参考读取。
    static constexpr std::size_t BaseBisectorDataOffset =
        DrawStateReadbackOffset + sizeof(CbtDrawState);
    // G 精确参考连续复制六个基础槽的十八个最终顶点。
    static constexpr std::size_t BaseRenderVertexOffset =
        BaseBisectorDataOffset + sizeof(CbtBisectorData) * CbtBaseBisectorCount;
    // 子分类位置与顶点位置分开校验，避免只验证渲染输出。
    static constexpr std::size_t BaseClassificationPositionOffset =
        BaseRenderVertexOffset +
        sizeof(Terrain::TerrainMeshVertex) * CbtBaseBisectorCount * 3U;
    // 父级辅助位置位于 classification buffer 的第四平面。
    static constexpr std::size_t BaseParentPositionOffset =
        BaseClassificationPositionOffset + sizeof(glm::vec3) * CbtBaseBisectorCount * 3U;
    // 普通路径消费稳定计数、OCBT 根与 draw state，不读取基础节点验证尾部。
    static constexpr std::size_t DiagnosticReadbackBytes = BaseBisectorDataOffset;
    // 最大范围覆盖全部诊断分段，作为每槽资源的统一容量。
    static constexpr std::size_t ValidationReadbackBytes =
        BaseParentPositionOffset + sizeof(glm::vec3) * CbtBaseBisectorCount;

    // 每个 swap-chain frame 分配一个 readback，复用槽位时 GPU 已完成该资源。
    [[nodiscard]] bool Initialize(ID3D12Device* device, std::string* errorMessage);
    void Shutdown();

    [[nodiscard]] ID3D12Resource* Readback(std::uint32_t frameIndex) const;
    // QueueSample 只发布元数据；它不映射资源，也不引入 CPU/GPU 同步点。
    void QueueSample(
        std::uint32_t frameIndex,
        std::uint64_t generation,
        bool validation,
        bool exactReference,
        const D3D12CbtDiagnosticExpectation& expectation);
    // ConsumeCompleted 在当前 frame index 再次可用时读取上一轮数据。
    [[nodiscard]] bool ConsumeCompleted(std::uint32_t frameIndex, std::string* errorMessage);
    // 首个持久拓扑错误会被锁存，恢复由上层整体替换 GPU state 完成。
    [[nodiscard]] bool LatchFault(const std::string& message, std::string* errorMessage);

    [[nodiscard]] const D3D12CbtDiagnosticSnapshot& Snapshot() const;
    [[nodiscard]] bool IsFaulted() const;
    [[nodiscard]] const std::string& FaultMessage() const;

private:
    static constexpr std::size_t FrameCount = Render::D3D12GraphicsBackend::FrameCount;

    // readback 与所有期望数组使用相同 frame index，避免跨槽引用失配。
    std::array<Microsoft::WRL::ComPtr<ID3D12Resource>, FrameCount> _readbacks;
    // Pending 在成功映射前保持置位，映射失败则同时锁存故障。
    std::array<bool, FrameCount> _pending{};
    // Generations 让错误信息对应 GPU 事务，而不是延迟后的显示帧。
    std::array<std::uint64_t, FrameCount> _generations{};
    // ValidationPending 控制大回读范围及 OCBT/draw state 的一致性检查。
    std::array<bool, FrameCount> _validationPending{};
    // ExactReferencePending 仅为初始化后的第一笔验证事务置位。
    std::array<bool, FrameCount> _exactReferencePending{};
    // Expectations 与提交它们的 readback 槽共同生存，直到槽位被消费。
    std::array<D3D12CbtDiagnosticExpectation, FrameCount> _expectations{};
    // Snapshot 是 renderer/status 面板读取的最后一个完整延迟样本。
    D3D12CbtDiagnosticSnapshot _snapshot;
    // FaultMessage 保留首错，防止后续派生错误遮盖根因。
    std::string _faultMessage;
    // Faulted 一旦置位，帧调度器会停止向损坏资源代继续写命令。
    bool _faulted{false};
};
} // namespace ParallelRoam::Algorithms::Cbt2024::D3D12
