#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>

namespace ParallelRoam::Algorithms::GpuRoam
{
inline constexpr std::size_t GpuRoamTimingReadbackSlotCount = 4U;

/// <summary>
/// GPU ROAM-like 路径持有的 OpenGL 资源和跨 pass 计数器布局
/// </summary>
struct GpuRoamCounters
{
    std::uint32_t ActiveLeafCount{0}; // compaction 输出长度
    std::uint32_t SplitCandidateCount{0}; // split 候选稠密长度
    std::uint32_t MergeCandidateCount{0}; // merge 候选稠密长度
    std::uint32_t Reserved{0}; // 保持着色器布局稳定
    std::uint32_t SplitOnlyCommitCount{0}; // 成功提交的 split 数
    std::uint32_t AllocatedNodeCount{0}; // 节点池原子尾指针
};

/// <summary>
/// DrawElementsIndirect command 的 GPU buffer 侧布局
/// </summary>
struct GpuRoamDrawElementsIndirectCommand
{
    std::uint32_t Count{0}; // 本帧有效索引数
    std::uint32_t InstanceCount{1}; // 地形只绘制一个实例
    std::uint32_t FirstIndex{0}; // 索引缓冲从零开始
    std::int32_t BaseVertex{0}; // 顶点缓冲不使用基址偏移
    std::uint32_t BaseInstance{0}; // 不使用实例数据偏移
};

/// <summary>
/// 延迟回读环中的单个 query 和 counter 配对槽位
/// </summary>
struct GpuRoamTimingReadbackSlot
{
    std::uint32_t TimerQueryId{0}; // 完整 compute 链的耗时查询
    std::uint32_t CounterBufferId{0}; // 与 query 同帧的计数器
    std::size_t CounterBufferCapacityBytes{0}; // counter buffer 当前容量
    std::size_t BaseActiveLeafCount{0}; // split 前的活动叶数量
    std::size_t BaseNodeCount{0}; // split 前的节点数量
    std::size_t ActiveLeafCapacity{0}; // 回读验证使用的叶容量
    std::size_t NodeCapacity{0}; // 回读验证使用的节点容量
    bool Pending{false}; // 槽位是否等待下一轮消费
};

/// <summary>
/// GPU ROAM-like builder 的可复用 OpenGL resource state
/// </summary>
class GpuRoamState
{
public:
    ~GpuRoamState();

    GpuRoamState() = default;
    GpuRoamState(const GpuRoamState&) = delete;
    GpuRoamState& operator=(const GpuRoamState&) = delete;
    GpuRoamState(GpuRoamState&&) = delete;
    GpuRoamState& operator=(GpuRoamState&&) = delete;

    void Reset();

    std::uint32_t NodeBufferId{0}; // CPU 快照和 GPU 新节点共享的池
    std::uint32_t ActiveLeafBufferId{0}; // compaction 稠密输出
    std::uint32_t HeightMapTextureId{0}; // 跨帧缓存的 R32F 高度图
    std::uint32_t ScreenErrorBufferId{0}; // 每个活动叶的评分
    std::uint32_t CounterBufferId{0}; // 指向当前回读槽位的别名
    std::uint32_t SplitCandidateBufferId{0}; // split 候选节点索引
    std::uint32_t MergeCandidateBufferId{0}; // merge 候选父节点索引
    std::uint32_t GpuVertexBufferId{0}; // emit 顶点输出
    std::uint32_t GpuIndexBufferId{0}; // emit 索引输出
    std::uint32_t IndirectDrawBufferId{0}; // GPU 绘制参数输出
    std::uint32_t ActiveLeafCompactionProgramId{0}; // 活动叶压缩 program
    std::uint32_t ErrorEvaluationProgramId{0}; // 误差计算 program
    std::uint32_t CandidateMarkingProgramId{0}; // 候选分类 program
    std::uint32_t MeshEmitProgramId{0}; // 网格生成 program
    std::uint32_t SplitOnlyTopologyProgramId{0}; // 拓扑提交 program
    std::size_t NodeBufferCapacityBytes{0}; // 节点池已分配字节数
    std::size_t ActiveLeafBufferCapacityBytes{0}; // 活动叶输出容量
    std::size_t ScreenErrorBufferCapacityBytes{0}; // 误差输出容量
    std::size_t SplitCandidateBufferCapacityBytes{0}; // split 候选容量
    std::size_t MergeCandidateBufferCapacityBytes{0}; // merge 候选容量
    std::size_t GpuVertexBufferCapacityBytes{0}; // 顶点输出容量
    std::size_t GpuIndexBufferCapacityBytes{0}; // 索引输出容量
    std::size_t IndirectDrawBufferCapacityBytes{0}; // 间接命令容量
    std::filesystem::path CachedHeightMapPath; // 高度图缓存键
    int CachedHeightMapWidth{0}; // 缓存纹理宽度
    int CachedHeightMapHeight{0}; // 缓存纹理高度
    bool HeightMapTextureUploaded{false}; // 缓存内容是否完整
    GpuRoamTimingReadbackSlot TimingReadbackSlots[GpuRoamTimingReadbackSlotCount]{}; // 延迟回读环
    std::size_t TimingReadbackCursor{0}; // 下一次复用的槽位
    GpuRoamCounters LastCompletedCounters{}; // 最近一次可靠计数
    float LastCompletedGpuComputeMilliseconds{0.0F}; // 最近一次可靠 GPU 时间
    bool HasCompletedTimingReadback{false}; // 最近结果是否可用于统计
};
} // namespace ParallelRoam::Algorithms::GpuRoam
