#include "algorithms/gpu_roam/GpuRoamMeshBuilder.h"

#include "algorithms/gpu_roam/GpuRoamActiveLeafCompaction.h"
#include "algorithms/gpu_roam/GpuRoamActiveLeafReset.h"
#include "algorithms/gpu_roam/GpuRoamBufferSchema.h"
#include "algorithms/gpu_roam/GpuRoamCandidateMarking.h"
#include "algorithms/gpu_roam/GpuRoamErrorEvaluation.h"
#include "algorithms/gpu_roam/GpuRoamMeshEmit.h"
#include "algorithms/gpu_roam/GpuRoamSplitOnlyTopology.h"
#include "platform/OpenGlCapabilities.h"
#include "tools/PerformanceTimer.h"

#include <glad/gl.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <sstream>
#include <string>
#include <vector>

namespace ParallelRoam::Algorithms::GpuRoam
{
namespace
{
constexpr std::size_t GpuPassIndex(GpuRoamGpuPass pass)
{
    return static_cast<std::size_t>(pass);
}

void CopyGpuPassTimingsToStats(const GpuRoamGpuPassTimings& timings, TerrainLodStats& stats)
{
    stats.GpuInitialLeafCompactionMilliseconds =
        timings.Milliseconds[GpuPassIndex(GpuRoamGpuPass::InitialLeafCompaction)];
    stats.GpuErrorEvaluationMilliseconds =
        timings.Milliseconds[GpuPassIndex(GpuRoamGpuPass::ErrorEvaluation)];
    stats.GpuSplitCandidateMarkingMilliseconds =
        timings.Milliseconds[GpuPassIndex(GpuRoamGpuPass::SplitCandidateMarking)];
    stats.GpuMergeCandidateMarkingMilliseconds =
        timings.Milliseconds[GpuPassIndex(GpuRoamGpuPass::MergeCandidateMarking)];
    stats.GpuSplitTopologyMilliseconds =
        timings.Milliseconds[GpuPassIndex(GpuRoamGpuPass::SplitTopology)];
    stats.GpuActiveLeafResetMilliseconds =
        timings.Milliseconds[GpuPassIndex(GpuRoamGpuPass::ActiveLeafReset)];
    stats.GpuFinalLeafCompactionMilliseconds =
        timings.Milliseconds[GpuPassIndex(GpuRoamGpuPass::FinalLeafCompaction)];
    stats.GpuMeshEmitMilliseconds =
        timings.Milliseconds[GpuPassIndex(GpuRoamGpuPass::MeshEmit)];
    stats.GpuPassSumMilliseconds = timings.SumMilliseconds();
}

struct GpuRoamUploadMetrics
{
    // 只记录 CPU 发起的数据传输时间，不包含 GPU 执行等待
    float CpuUploadMilliseconds{0.0F};
    // 资源扩容单独计时，避免把偶发分配抖动归入稳定上传成本
    float BufferAllocationMilliseconds{0.0F};
};

std::size_t NormalizedTriangleBudget(const TerrainLodBuildInput& input)
{
    // 两个 root 是矩形地形完整覆盖所需的最小活动集合。
    return std::max<std::size_t>(input.Settings.TriangleBudget, 2U);
}

std::uint32_t SaturateToUint32(std::size_t value)
{
    // GPU counter ABI 固定为 32 位，饱和转换避免超大宿主配置回绕成小预算。
    return static_cast<std::uint32_t>(std::min<std::size_t>(
        value,
        std::numeric_limits<std::uint32_t>::max()));
}

std::size_t RemainingSplitBudget(const GpuRoamBufferSnapshot& snapshot, const TerrainLodBuildInput& input)
{
    // CPU DOD baseline 已消费的 leaf 与 GPU split-only pass 共用硬上限。
    const std::size_t triangleBudget = NormalizedTriangleBudget(input);
    return triangleBudget > snapshot.ActiveLeafIndices.size()
        ? triangleBudget - snapshot.ActiveLeafIndices.size()
        : 0U;
}

std::size_t AdditionalGpuSplitCapacity(const GpuRoamBufferSnapshot& snapshot, const TerrainLodBuildInput& input)
{
    // 一轮 pass 中每个输入 leaf 最多成功 split 一次。
    return std::min(snapshot.ActiveLeafIndices.size(), RemainingSplitBudget(snapshot, input));
}

std::string BuildGpuStatusMessage(bool usesIndirectDraw)
{
    if (usesIndirectDraw)
    {
        return "GPU ROAM-like: CPU DOD merge baseline, GPU split/direct-diamond, mesh emit and indirect draw";
    }

    return "GPU ROAM-like: CPU DOD merge baseline, GPU split/direct-diamond, mesh emit and GPU buffer draw";
}

bool EnsureBufferCapacity(
    GLenum target,
    std::uint32_t& bufferId,
    std::size_t& currentCapacityBytes,
    std::size_t requiredCapacityBytes,
    GpuRoamUploadMetrics& metrics,
    std::string* errorMessage)
{
    // OpenGL 不允许创建零大小存储，空输入仍保留一个字节作为合法占位
    const std::size_t safeRequiredCapacityBytes = std::max<std::size_t>(requiredCapacityBytes, 1U);
    if (bufferId == 0U)
    {
        // bufferId 是 builder 持久状态，首次使用时才创建对象
        GLuint nextBufferId = 0U;
        glGenBuffers(1, &nextBufferId);
        bufferId = nextBufferId;
    }

    if (bufferId == 0U)
    {
        if (errorMessage != nullptr)
        {
            *errorMessage = "GPU ROAM-like buffer allocation failed";
        }
        return false;
    }

    if (currentCapacityBytes < safeRequiredCapacityBytes)
    {
        // 容量只增长不缩小，连续帧拓扑波动不会反复触发重新分配
        Tools::PerformanceTimer allocationTimer;
        glBindBuffer(target, bufferId);
        // 传入空数据只分配存储，实际快照由后续 glBufferSubData 写入
        glBufferData(target, static_cast<GLsizeiptr>(safeRequiredCapacityBytes), nullptr, GL_DYNAMIC_DRAW);
        glBindBuffer(target, 0);
        metrics.BufferAllocationMilliseconds += allocationTimer.Stop();
        currentCapacityBytes = safeRequiredCapacityBytes;
    }
    return true;
}

bool UploadBufferRange(
    GLenum target,
    std::uint32_t& bufferId,
    std::size_t& currentCapacityBytes,
    const void* data,
    std::size_t dataByteCount,
    std::size_t capacityByteCount,
    GpuRoamUploadMetrics& metrics,
    std::string* errorMessage)
{
    // 数据字节数和预留容量分开传递，调用点必须先证明上传不会越界
    if (dataByteCount > capacityByteCount)
    {
        if (errorMessage != nullptr)
        {
            *errorMessage = "GPU ROAM-like buffer allocation failed";
        }
        return false;
    }

    if (!EnsureBufferCapacity(target, bufferId, currentCapacityBytes, capacityByteCount, metrics, errorMessage))
    {
        return false;
    }

    if (data != nullptr && dataByteCount > 0U)
    {
        // 只更新有效前缀，缓冲尾部留给本帧 GPU 分裂产生的新节点
        Tools::PerformanceTimer uploadTimer;
        glBindBuffer(target, bufferId);
        glBufferSubData(target, 0, static_cast<GLsizeiptr>(dataByteCount), data);
        glBindBuffer(target, 0);
        metrics.CpuUploadMilliseconds += uploadTimer.Stop();
    }
    return true;
}

bool HeightMapTextureMatches(const GpuRoamState& state, const Terrain::HeightMap& heightMap)
{
    // 路径和尺寸共同构成缓存键，避免同名不同尺寸资源被错误复用
    return state.HeightMapTextureUploaded &&
           state.HeightMapTextureId != 0U &&
           state.CachedHeightMapWidth == heightMap.Width() &&
           state.CachedHeightMapHeight == heightMap.Height() &&
           state.CachedHeightMapPath == heightMap.SourcePath();
}

bool UploadHeightMapTextureIfNeeded(
    const Terrain::HeightMap& heightMap,
    GpuRoamState& state,
    std::size_t& uploadBytes,
    GpuRoamUploadMetrics& metrics,
    std::string* errorMessage)
{
    if (HeightMapTextureMatches(state, heightMap))
    {
        // 高度图通常跨多帧不变，命中后完全跳过 CPU 展平和纹理上传
        return true;
    }

    if (state.HeightMapTextureId == 0U)
    {
        GLuint nextTextureId = 0U;
        glGenTextures(1, &nextTextureId);
        state.HeightMapTextureId = nextTextureId;
    }

    if (state.HeightMapTextureId == 0U)
    {
        if (errorMessage != nullptr)
        {
            *errorMessage = "GPU ROAM-like height map texture allocation failed";
        }
        return false;
    }

    Tools::PerformanceTimer uploadTimer;
    // HeightMap 的抽象采样接口不保证底层连续，因此在上传前显式展平
    std::vector<float> heights;
    heights.resize(static_cast<std::size_t>(heightMap.Width()) * static_cast<std::size_t>(heightMap.Height()));
    for (int y = 0; y < heightMap.Height(); ++y)
    {
        for (int x = 0; x < heightMap.Width(); ++x)
        {
            const auto index = static_cast<std::size_t>(y) * static_cast<std::size_t>(heightMap.Width()) +
                               static_cast<std::size_t>(x);
            heights[index] = heightMap.SamplePixel(x, y);
        }
    }

    glBindTexture(GL_TEXTURE_2D, state.HeightMapTextureId);
    // 误差评估和法线重建都需要连续采样，使用线性过滤和边缘夹取
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(
        GL_TEXTURE_2D,
        0,
        GL_R32F,
        heightMap.Width(),
        heightMap.Height(),
        0,
        GL_RED,
        GL_FLOAT,
        heights.data());
    glBindTexture(GL_TEXTURE_2D, 0);
    // 高度图字节数只在缓存失效帧计入上传统计
    uploadBytes += heights.size() * sizeof(float);
    metrics.CpuUploadMilliseconds += uploadTimer.Stop();
    state.CachedHeightMapPath = heightMap.SourcePath();
    state.CachedHeightMapWidth = heightMap.Width();
    state.CachedHeightMapHeight = heightMap.Height();
    state.HeightMapTextureUploaded = true;
    return true;
}

bool ResolveTimingReadbackSlot(
    GpuRoamState& state,
    std::size_t slotIndex,
    GpuRoamGpuPassTimings& gpuPassTimings,
    std::size_t& readbackBytes,
    float& queryWaitMilliseconds,
    float& readbackWaitMilliseconds,
    std::string* errorMessage)
{
    GpuRoamTimingReadbackSlot& slot = state.TimingReadbackSlots[slotIndex];
    gpuPassTimings = state.HasCompletedTimingReadback
        ? state.LastCompletedGpuPassTimings
        : GpuRoamGpuPassTimings{};

    if (!slot.Pending)
    {
        // 首轮使用或已经消费过的槽位没有可读结果
        return true;
    }

    bool allQueriesAvailable = true;
    Tools::PerformanceTimer queryWaitTimer;
    std::array<GLuint64, GpuRoamGpuPassCount> elapsedNanoseconds{};
    for (std::size_t passIndex = 0; passIndex < GpuRoamGpuPassCount; ++passIndex)
    {
        GLuint queryAvailable = GL_FALSE;
        glGetQueryObjectuiv(
            slot.TimerQueryIds[passIndex],
            GL_QUERY_RESULT_AVAILABLE,
            &queryAvailable);
        allQueriesAvailable = allQueriesAvailable && queryAvailable == GL_TRUE;
        glGetQueryObjectui64v(
            slot.TimerQueryIds[passIndex],
            GL_QUERY_RESULT,
            &elapsedNanoseconds[passIndex]);
    }
    queryWaitMilliseconds += queryWaitTimer.Stop();
    // OpenGL keeps timer results in query objects rather than a readback buffer;
    // count the logical seven 64-bit results so the CSV still exposes their transfer size.
    readbackBytes += GpuRoamGpuPassCount * sizeof(GLuint64);

    GpuRoamCounters counters{};
    // counter buffer 与 timer query 使用同一槽位，二者描述的是同一次 dispatch 链
    Tools::PerformanceTimer readbackTimer;
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, slot.CounterBufferId);
    glGetBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, static_cast<GLsizeiptr>(sizeof(counters)), &counters);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
    readbackWaitMilliseconds += readbackTimer.Stop();
    readbackBytes += sizeof(counters);

    const std::size_t expectedActiveLeafCount =
        slot.BaseActiveLeafCount + static_cast<std::size_t>(counters.SplitOnlyCommitCount);
    // 每次成功 split 消耗一个叶节点并产生两个子节点，净增一个活动叶和两个节点
    const std::size_t expectedAllocatedNodeCount =
        slot.BaseNodeCount + static_cast<std::size_t>(counters.SplitOnlyCommitCount) * 2U;
    if (counters.ActiveLeafCount != expectedActiveLeafCount ||
        counters.AllocatedNodeCount != expectedAllocatedNodeCount ||
        static_cast<std::size_t>(counters.RemainingSplitBudget) +
                static_cast<std::size_t>(counters.SplitOnlyCommitCount) !=
            slot.InitialSplitBudget ||
        expectedActiveLeafCount > slot.ActiveLeafCapacity ||
        expectedAllocatedNodeCount > slot.NodeCapacity)
    {
        if (errorMessage != nullptr)
        {
            std::ostringstream stream;
            stream << "GPU ROAM-like delayed topology count mismatch: active leaves gpu="
                   << counters.ActiveLeafCount << " expected=" << expectedActiveLeafCount
                   << ", allocated nodes gpu=" << counters.AllocatedNodeCount
                   << " expected=" << expectedAllocatedNodeCount
                   << ", allQueriesAvailable=" << (allQueriesAvailable ? "true" : "false");
            *errorMessage = stream.str();
        }
        slot.Pending = false;
        return false;
    }

    state.LastCompletedCounters = counters;
    for (std::size_t passIndex = 0; passIndex < GpuRoamGpuPassCount; ++passIndex)
    {
        state.LastCompletedGpuPassTimings.Milliseconds[passIndex] =
            static_cast<float>(static_cast<double>(elapsedNanoseconds[passIndex]) / 1'000'000.0);
    }
    state.HasCompletedTimingReadback = true;
    gpuPassTimings = state.LastCompletedGpuPassTimings;
    slot.Pending = false;
    return true;
}

bool EnsureTimingReadbackSlot(
    GpuRoamState& state,
    std::size_t slotIndex,
    GpuRoamUploadMetrics& metrics,
    std::string* errorMessage)
{
    GpuRoamTimingReadbackSlot& slot = state.TimingReadbackSlots[slotIndex];
    const bool needsQueries = std::any_of(
        slot.TimerQueryIds.begin(),
        slot.TimerQueryIds.end(),
        [](std::uint32_t queryId) { return queryId == 0U; });
    if (needsQueries)
    {
        std::array<GLuint, GpuRoamGpuPassCount> queryIds{};
        glGenQueries(static_cast<GLsizei>(queryIds.size()), queryIds.data());
        std::copy(queryIds.begin(), queryIds.end(), slot.TimerQueryIds.begin());
    }

    if (std::any_of(
            slot.TimerQueryIds.begin(),
            slot.TimerQueryIds.end(),
            [](std::uint32_t queryId) { return queryId == 0U; }))
    {
        if (errorMessage != nullptr)
        {
            *errorMessage = "GPU ROAM-like timer query allocation failed";
        }
        return false;
    }

    if (!EnsureBufferCapacity(
            GL_SHADER_STORAGE_BUFFER,
            slot.CounterBufferId,
            slot.CounterBufferCapacityBytes,
            sizeof(GpuRoamCounters),
            metrics,
            errorMessage))
    {
        return false;
    }

    // 后续 pass 通过统一 CounterBufferId 绑定当前轮转槽位
    state.CounterBufferId = slot.CounterBufferId;
    return true;
}
} // namespace

bool GpuRoamMeshBuilder::Build(
    const GpuRoamBufferSnapshot& snapshot,
    const TerrainLodBuildInput& input,
    TerrainLodRenderPacket& outPacket,
    TerrainLodStats& inOutStats,
    std::string* errorMessage)
{
    // 最坏情况下每个输入叶节点成功一次 split，因此节点池额外预留两个节点
    const std::size_t gpuNodeCapacity =
        snapshot.Nodes.size() + AdditionalGpuSplitCapacity(snapshot, input) * 2U;
    std::size_t uploadBytes = 0U;
    float cpuUploadMilliseconds = 0.0F;
    float bufferAllocationMilliseconds = 0.0F;
    if (!UploadSnapshot(
            snapshot,
            *input.HeightMap,
            gpuNodeCapacity,
            uploadBytes,
            cpuUploadMilliseconds,
            bufferAllocationMilliseconds,
            errorMessage))
    {
        return false;
    }

    // GPU 数量采用延迟回读，当前帧不会为了统计数据阻塞命令执行
    GpuRoamGpuPassTimings gpuPassTimings{};
    std::size_t readbackBytes = 0U;
    float dispatchWallMilliseconds = 0.0F;
    float queryWaitMilliseconds = 0.0F;
    float readbackWaitMilliseconds = 0.0F;
    std::size_t gpuActiveLeafCount = snapshot.ActiveLeafIndices.size();
    std::size_t gpuNodeCount = snapshot.Nodes.size();
    std::size_t gpuSplitOnlyCommitCount = 0U;
    if (!RunGpuAlgorithmPasses(
            snapshot,
            input,
            uploadBytes,
            gpuPassTimings,
            readbackBytes,
            bufferAllocationMilliseconds,
            dispatchWallMilliseconds,
            queryWaitMilliseconds,
            readbackWaitMilliseconds,
            gpuActiveLeafCount,
            gpuNodeCount,
            gpuSplitOnlyCommitCount,
            errorMessage))
    {
        return false;
    }

    // 分项统计保留上传、调度、查询等待和回读等待，便于区分 CPU 与 GPU 瓶颈
    inOutStats.CpuGpuUploadBytes = uploadBytes;
    inOutStats.CpuGpuReadbackBytes = readbackBytes;
    CopyGpuPassTimingsToStats(gpuPassTimings, inOutStats);
    inOutStats.CpuUploadMilliseconds = cpuUploadMilliseconds;
    inOutStats.GpuBufferAllocationMilliseconds = bufferAllocationMilliseconds;
    inOutStats.GpuDispatchWallMilliseconds = dispatchWallMilliseconds;
    inOutStats.GpuQueryWaitMilliseconds = queryWaitMilliseconds;
    inOutStats.GpuReadbackWaitMilliseconds = readbackWaitMilliseconds;
    if (gpuSplitOnlyCommitCount > 0U)
    {
        // 只有已经完成的延迟回读才覆盖 CPU 基线数量
        inOutStats.ActiveTriangleCount = gpuActiveLeafCount;
        inOutStats.ActiveNodeCount = std::max(inOutStats.ActiveNodeCount, gpuNodeCount);
        inOutStats.SplitCount += gpuSplitOnlyCommitCount;
    }
    inOutStats.BudgetRejectedSplitCount += _state.LastCompletedCounters.BudgetRejectedSplitCount;

    const Platform::OpenGlGpuCapabilities gpuCapabilities = Platform::QueryOpenGlGpuCapabilities();
    // 间接绘制不可用时仍复用 GPU 生成的 VBO 和 IBO，由 CPU 提供 draw count
    const bool usesIndirectDraw = gpuCapabilities.SupportsIndirectDraw && _state.IndirectDrawBufferId != 0U;
    outPacket.Mode = usesIndirectDraw ? TerrainLodRenderMode::GpuIndirect : TerrainLodRenderMode::GpuBuffers;
    outPacket.StatusMessage = BuildGpuStatusMessage(usesIndirectDraw);
    outPacket.GpuNodeBufferId = _state.NodeBufferId;
    outPacket.GpuHeightMapTextureId = _state.HeightMapTextureId;
    outPacket.GpuVertexBufferId = _state.GpuVertexBufferId;
    outPacket.GpuIndexBufferId = _state.GpuIndexBufferId;
    outPacket.ActiveLeafBufferId = _state.ActiveLeafBufferId;
    outPacket.IndirectDrawBufferId = usesIndirectDraw ? _state.IndirectDrawBufferId : 0U;
    outPacket.GpuResourceLifetime = TerrainLodGpuResourceLifetime::UntilNextBuildOrReset;
    // generation 绑定 CPU 快照序列，渲染器可据此拒绝跨 build 缓存
    outPacket.GpuResourceGeneration = snapshot.BuildSequence;
    outPacket.ActiveLeafCount = gpuActiveLeafCount;
    outPacket.ActiveTriangleCount = inOutStats.ActiveTriangleCount;
    outPacket.IndexCount = gpuActiveLeafCount * 3U;
    // 统一边界在返回前同时验证数量和 GPU 资源所有权契约
    return outPacket.ActiveTriangleCount > 0U &&
           outPacket.IndexCount > 0U &&
           outPacket.GpuVertexBufferId != 0U &&
           outPacket.GpuIndexBufferId != 0U &&
           (outPacket.Mode != TerrainLodRenderMode::GpuIndirect || outPacket.IndirectDrawBufferId != 0U) &&
           outPacket.HasConsistentResourceContract();
}

void GpuRoamMeshBuilder::Reset()
{
    // Reset 会销毁 OpenGL 对象，调用方必须保证当前上下文仍然有效
    _state.Reset();
}

bool GpuRoamMeshBuilder::UploadSnapshot(
    const GpuRoamBufferSnapshot& snapshot,
    const Terrain::HeightMap& heightMap,
    std::size_t nodeCapacity,
    std::size_t& uploadBytes,
    float& cpuUploadMilliseconds,
    float& bufferAllocationMilliseconds,
    std::string* errorMessage)
{
    GpuRoamUploadMetrics metrics{};
    // 节点缓冲上传 CPU 快照前缀，剩余容量供 compute shader 原子分配子节点
    if (!UploadBufferRange(
            GL_SHADER_STORAGE_BUFFER,
            _state.NodeBufferId,
            _state.NodeBufferCapacityBytes,
            snapshot.Nodes.data(),
            snapshot.NodeBufferBytes(),
            nodeCapacity * sizeof(GpuRoamNodeRecord),
            metrics,
            errorMessage))
    {
        return false;
    }
    uploadBytes += snapshot.NodeBufferBytes();

    // 活动叶由 compaction pass 生成，CPU 不上传快照中的索引数组
    if (!EnsureBufferCapacity(
            GL_SHADER_STORAGE_BUFFER,
            _state.ActiveLeafBufferId,
            _state.ActiveLeafBufferCapacityBytes,
            nodeCapacity * sizeof(std::uint32_t),
            metrics,
            errorMessage))
    {
        return false;
    }

    if (!UploadHeightMapTextureIfNeeded(heightMap, _state, uploadBytes, metrics, errorMessage))
    {
        return false;
    }
    // 调用者把本方法计时与 compute dispatch 和 readback 分开汇总
    cpuUploadMilliseconds = metrics.CpuUploadMilliseconds;
    bufferAllocationMilliseconds = metrics.BufferAllocationMilliseconds;
    return true;
}

bool GpuRoamMeshBuilder::RunGpuAlgorithmPasses(
    const GpuRoamBufferSnapshot& snapshot,
    const TerrainLodBuildInput& input,
    std::size_t& uploadBytes,
    GpuRoamGpuPassTimings& gpuPassTimings,
    std::size_t& readbackBytes,
    float& bufferAllocationMilliseconds,
    float& dispatchWallMilliseconds,
    float& queryWaitMilliseconds,
    float& readbackWaitMilliseconds,
    std::size_t& gpuActiveLeafCount,
    std::size_t& gpuNodeCount,
    std::size_t& gpuSplitOnlyCommitCount,
    std::string* errorMessage)
{
    // program 对象按需编译并缓存在 state 中，失败时不提交任何 compute 工作
    if (!EnsureGpuRoamActiveLeafCompactionProgram(_state.ActiveLeafCompactionProgramId, errorMessage) ||
        !EnsureGpuRoamActiveLeafResetProgram(_state.ActiveLeafResetProgramId, errorMessage) ||
        !EnsureGpuRoamErrorEvaluationProgram(_state.ErrorEvaluationProgramId, errorMessage) ||
        !EnsureGpuRoamCandidateMarkingProgram(_state.CandidateMarkingProgramId, errorMessage) ||
        !EnsureGpuRoamMeshEmitProgram(_state.MeshEmitProgramId, errorMessage))
    {
        return false;
    }

    const std::size_t nodeCount = snapshot.Nodes.size();
    const std::size_t activeLeafCount = snapshot.ActiveLeafIndices.size();
    const std::size_t remainingSplitBudget = RemainingSplitBudget(snapshot, input);
    const std::size_t additionalSplitCapacity = AdditionalGpuSplitCapacity(snapshot, input);
    // 每次成功 split 净增一个活动 leaf，并追加两个节点；容量只为统一预算内的提交预留。
    // 预算耗尽时两种容量都退化为 CPU 快照的实际工作集。
    const std::size_t nodeCapacity = nodeCount + additionalSplitCapacity * 2U;
    const std::size_t activeLeafCapacity = activeLeafCount + additionalSplitCapacity;
    // 误差数组按输入活动叶索引寻址，不按完整节点池寻址
    const std::size_t screenErrorBytes = activeLeafCount * sizeof(float);
    // 候选缓冲按节点索引存储，至少分配一个元素以满足 OpenGL 资源规则
    const std::size_t candidateBufferBytes = std::max<std::size_t>(nodeCapacity, 1U) * sizeof(std::uint32_t);
    // emit 为每个最终活动叶写三个不共享顶点，避免跨线程去重同步
    const std::size_t vertexBufferBytes =
        activeLeafCapacity * 3U * sizeof(Terrain::TerrainMeshVertex);
    const std::size_t indexBufferBytes =
        activeLeafCapacity * 3U * sizeof(std::uint32_t);
    const GpuRoamDrawElementsIndirectCommand emptyIndirectCommand{};

    GpuRoamUploadMetrics metrics{};
    // 先消费即将复用的旧槽位，再把它重置为本帧计时和 counter 目标
    const std::size_t timingSlotIndex = _state.TimingReadbackCursor % GpuRoamTimingReadbackSlotCount;
    if (!ResolveTimingReadbackSlot(
            _state,
            timingSlotIndex,
            gpuPassTimings,
            readbackBytes,
            queryWaitMilliseconds,
            readbackWaitMilliseconds,
            errorMessage) ||
        !EnsureTimingReadbackSlot(_state, timingSlotIndex, metrics, errorMessage))
    {
        return false;
    }

    GpuRoamCounters zeroCounters{};
    // 节点池尾指针从 CPU 快照末尾开始，shader 只在该位置之后追加节点
    zeroCounters.AllocatedNodeCount = SaturateToUint32(nodeCount);
    // counter 中的 token 会被所有并发 split invocation 原子共享。
    zeroCounters.RemainingSplitBudget = SaturateToUint32(remainingSplitBudget);
    // 所有 pass 输出在 dispatch 前一次性扩容，执行期间不允许资源对象发生变化
    if (!EnsureBufferCapacity(
            GL_SHADER_STORAGE_BUFFER,
            _state.ScreenErrorBufferId,
            _state.ScreenErrorBufferCapacityBytes,
            screenErrorBytes,
            metrics,
            errorMessage) ||
        !EnsureBufferCapacity(
            GL_SHADER_STORAGE_BUFFER,
            _state.SplitCandidateBufferId,
            _state.SplitCandidateBufferCapacityBytes,
            candidateBufferBytes,
            metrics,
            errorMessage) ||
        !EnsureBufferCapacity(
            GL_SHADER_STORAGE_BUFFER,
            _state.MergeCandidateBufferId,
            _state.MergeCandidateBufferCapacityBytes,
            candidateBufferBytes,
            metrics,
            errorMessage) ||
        !EnsureBufferCapacity(
            GL_SHADER_STORAGE_BUFFER,
            _state.GpuVertexBufferId,
            _state.GpuVertexBufferCapacityBytes,
            vertexBufferBytes,
            metrics,
            errorMessage) ||
        !EnsureBufferCapacity(
            GL_SHADER_STORAGE_BUFFER,
            _state.GpuIndexBufferId,
            _state.GpuIndexBufferCapacityBytes,
            indexBufferBytes,
            metrics,
            errorMessage) ||
        !UploadBufferRange(
            GL_SHADER_STORAGE_BUFFER,
            _state.IndirectDrawBufferId,
            _state.IndirectDrawBufferCapacityBytes,
            &emptyIndirectCommand,
            sizeof(emptyIndirectCommand),
            sizeof(emptyIndirectCommand),
            metrics,
            errorMessage) ||
        !UploadBufferRange(
            GL_SHADER_STORAGE_BUFFER,
            _state.CounterBufferId,
            _state.TimingReadbackSlots[timingSlotIndex].CounterBufferCapacityBytes,
            &zeroCounters,
            sizeof(zeroCounters),
            sizeof(zeroCounters),
            metrics,
            errorMessage))
    {
        return false;
    }
    uploadBytes += sizeof(zeroCounters);
    uploadBytes += sizeof(emptyIndirectCommand);
    // 间接命令必须先清零，emit 未产生有效叶时也不会沿用上一帧 draw count
    bufferAllocationMilliseconds += metrics.BufferAllocationMilliseconds;

    Tools::PerformanceTimer dispatchWallTimer;
    GpuRoamTimingReadbackSlot& slot = _state.TimingReadbackSlots[timingSlotIndex];

    // 首次压缩只扫描 CPU 快照已有节点，为误差评估建立稠密活动叶列表
    GpuRoamActiveLeafCompactionPassInput compactionInput{};
    compactionInput.ProgramId = _state.ActiveLeafCompactionProgramId;
    compactionInput.NodeBufferId = _state.NodeBufferId;
    compactionInput.ActiveLeafBufferId = _state.ActiveLeafBufferId;
    compactionInput.CounterBufferId = _state.CounterBufferId;
    compactionInput.NodeCount = nodeCount;
    glBeginQuery(
        GL_TIME_ELAPSED,
        slot.TimerQueryIds[GpuPassIndex(GpuRoamGpuPass::InitialLeafCompaction)]);
    RunGpuRoamActiveLeafCompactionPass(compactionInput);
    glEndQuery(GL_TIME_ELAPSED);

    // 误差 pass 只写 screen error，不修改节点拓扑
    GpuRoamErrorEvaluationPassInput errorInput{};
    errorInput.ProgramId = _state.ErrorEvaluationProgramId;
    errorInput.NodeBufferId = _state.NodeBufferId;
    errorInput.ActiveLeafBufferId = _state.ActiveLeafBufferId;
    errorInput.ScreenErrorBufferId = _state.ScreenErrorBufferId;
    errorInput.HeightMapTextureId = _state.HeightMapTextureId;
    errorInput.ActiveLeafCount = activeLeafCount;
    errorInput.TerrainSize = input.Settings.TerrainSize;
    errorInput.HeightScale = input.Settings.HeightScale;
    errorInput.ViewProjection = input.View.ViewProjection;
    errorInput.FrustumPlanes = input.View.FrustumPlanes;
    errorInput.DrawableWidth = std::max(input.View.DrawableWidth, 1U);
    errorInput.DrawableHeight = std::max(input.View.DrawableHeight, 1U);
    glBeginQuery(
        GL_TIME_ELAPSED,
        slot.TimerQueryIds[GpuPassIndex(GpuRoamGpuPass::ErrorEvaluation)]);
    RunGpuRoamErrorEvaluationPass(errorInput);
    glEndQuery(GL_TIME_ELAPSED);

    // 候选 pass 读取同一活动叶顺序，使 screen error 与叶索引一一对应
    GpuRoamCandidateMarkingPassInput candidateInput{};
    candidateInput.ProgramId = _state.CandidateMarkingProgramId;
    candidateInput.NodeBufferId = _state.NodeBufferId;
    candidateInput.ActiveLeafBufferId = _state.ActiveLeafBufferId;
    candidateInput.ScreenErrorBufferId = _state.ScreenErrorBufferId;
    candidateInput.CounterBufferId = _state.CounterBufferId;
    candidateInput.SplitCandidateBufferId = _state.SplitCandidateBufferId;
    candidateInput.MergeCandidateBufferId = _state.MergeCandidateBufferId;
    candidateInput.HeightMapTextureId = _state.HeightMapTextureId;
    candidateInput.NodeCount = nodeCount;
    candidateInput.ActiveLeafLimit = activeLeafCount;
    candidateInput.MaxDepth = input.Settings.MaxDepth;
    candidateInput.TerrainSize = input.Settings.TerrainSize;
    candidateInput.HeightScale = input.Settings.HeightScale;
    candidateInput.SplitThreshold = input.Settings.ScreenSpaceSplitThresholdPixels;
    // merge 只输出 GPU 统计；真正的动态级联合并已在本帧 DOD baseline 中提交。
    candidateInput.MergeThreshold = input.Settings.ScreenSpaceMergeThresholdPixels;
    candidateInput.ViewProjection = errorInput.ViewProjection;
    candidateInput.FrustumPlanes = input.View.FrustumPlanes;
    candidateInput.DrawableWidth = errorInput.DrawableWidth;
    candidateInput.DrawableHeight = errorInput.DrawableHeight;
    candidateInput.Kind = GpuRoamCandidateKind::Split;
    glBeginQuery(
        GL_TIME_ELAPSED,
        slot.TimerQueryIds[GpuPassIndex(GpuRoamGpuPass::SplitCandidateMarking)]);
    RunGpuRoamCandidateMarkingPass(candidateInput);
    glEndQuery(GL_TIME_ELAPSED);

    // Merge scoring is a separate ROAM decision stage. It produces diagnostic
    // candidates only; CPU DOD already committed the persistent merge topology.
    candidateInput.Kind = GpuRoamCandidateKind::Merge;
    glBeginQuery(
        GL_TIME_ELAPSED,
        slot.TimerQueryIds[GpuPassIndex(GpuRoamGpuPass::MergeCandidateMarking)]);
    RunGpuRoamCandidateMarkingPass(candidateInput);
    glEndQuery(GL_TIME_ELAPSED);

    // 拓扑 pass 通过原子锁和容量检查提交一轮兼容成对分裂
    GpuRoamSplitOnlyTopologyPassInput splitOnlyInput{};
    splitOnlyInput.NodeBufferId = _state.NodeBufferId;
    splitOnlyInput.SplitCandidateBufferId = _state.SplitCandidateBufferId;
    splitOnlyInput.CounterBufferId = _state.CounterBufferId;
    splitOnlyInput.CandidateDispatchCount = activeLeafCount;
    splitOnlyInput.NodeCapacity = nodeCapacity;
    splitOnlyInput.MaxDepth = input.Settings.MaxDepth;
    splitOnlyInput.BuildSequence = snapshot.BuildSequence;
    glBeginQuery(
        GL_TIME_ELAPSED,
        slot.TimerQueryIds[GpuPassIndex(GpuRoamGpuPass::SplitTopology)]);
    const bool splitSucceeded = RunGpuRoamSplitOnlyTopologyPass(
        _state.SplitOnlyTopologyProgramId,
        splitOnlyInput,
        errorMessage);
    glEndQuery(GL_TIME_ELAPSED);
    if (!splitSucceeded)
    {
        return false;
    }

    // split 改变叶节点集合，第二次 compaction 前由独立 shader 只重置活动叶计数
    glBeginQuery(
        GL_TIME_ELAPSED,
        slot.TimerQueryIds[GpuPassIndex(GpuRoamGpuPass::ActiveLeafReset)]);
    RunGpuRoamActiveLeafResetPass(_state.ActiveLeafResetProgramId, _state.CounterBufferId);
    glEndQuery(GL_TIME_ELAPSED);

    // 扫描完整预留节点池，但 shader 通过 AllocatedNodeCount 排除未分配尾部
    compactionInput.NodeCount = nodeCapacity;
    glBeginQuery(
        GL_TIME_ELAPSED,
        slot.TimerQueryIds[GpuPassIndex(GpuRoamGpuPass::FinalLeafCompaction)]);
    RunGpuRoamActiveLeafCompactionPass(compactionInput);
    glEndQuery(GL_TIME_ELAPSED);

    // emit 消费最终活动叶集合并同时写顶点、索引和间接绘制命令
    GpuRoamMeshEmitPassInput emitInput{};
    emitInput.ProgramId = _state.MeshEmitProgramId;
    emitInput.NodeBufferId = _state.NodeBufferId;
    emitInput.ActiveLeafBufferId = _state.ActiveLeafBufferId;
    emitInput.CounterBufferId = _state.CounterBufferId;
    emitInput.VertexBufferId = _state.GpuVertexBufferId;
    emitInput.IndexBufferId = _state.GpuIndexBufferId;
    emitInput.IndirectDrawBufferId = _state.IndirectDrawBufferId;
    emitInput.HeightMapTextureId = _state.HeightMapTextureId;
    emitInput.ActiveLeafCapacity = activeLeafCapacity;
    emitInput.NodeCapacity = nodeCapacity;
    emitInput.MaxDepth = input.Settings.MaxDepth;
    emitInput.BuildSequence = snapshot.BuildSequence;
    emitInput.TerrainSize = input.Settings.TerrainSize;
    emitInput.HeightScale = input.Settings.HeightScale;
    glBeginQuery(
        GL_TIME_ELAPSED,
        slot.TimerQueryIds[GpuPassIndex(GpuRoamGpuPass::MeshEmit)]);
    RunGpuRoamMeshEmitPass(emitInput);
    glEndQuery(GL_TIME_ELAPSED);
    // wall 时间仅表示 CPU 发出 pass 链的耗时，不等同于 GPU 执行时间
    dispatchWallMilliseconds += dispatchWallTimer.Stop();

    glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
    glBindTexture(GL_TEXTURE_2D, 0);

    // 保存本帧基线数量，延迟回读时才能验证 split 后的计数守恒
    slot.BaseActiveLeafCount = activeLeafCount;
    slot.BaseNodeCount = nodeCount;
    slot.ActiveLeafCapacity = activeLeafCapacity;
    slot.NodeCapacity = nodeCapacity;
    slot.InitialSplitBudget = SaturateToUint32(remainingSplitBudget);
    slot.Pending = true;
    // 轮转槽位让 GPU 有多个帧间隔完成 query 和 counter 写入
    _state.TimingReadbackCursor = (_state.TimingReadbackCursor + 1U) % GpuRoamTimingReadbackSlotCount;

    // 当前帧不再同步等待 GPU counter，Renderer 对 indirect draw 使用 GPU 端 command
    // CPU 统计保留 DOD baseline，延迟读回只用于后续 timing 和防御验证
    gpuActiveLeafCount = activeLeafCount;
    gpuNodeCount = nodeCount;
    gpuSplitOnlyCommitCount = 0U;

    return true;
}
} // namespace ParallelRoam::Algorithms::GpuRoam
