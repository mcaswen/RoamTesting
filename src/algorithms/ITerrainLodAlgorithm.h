#pragma once

#include "terrain/HeightMap.h"
#include "terrain/TerrainMeshBuilder.h"

#include <glm/glm.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace ParallelRoam::Algorithms
{
/// <summary>
/// 标识当前 terrain LOD 算法实现，供 UI、benchmark 和日志输出使用
/// </summary>
enum class TerrainLodAlgorithmId
{
    ClassicCpuRoam,
    DataOrientedCpuRoam,
    GpuRoamLike,
    Cbt2024,
    Count,
};

/// <summary>
/// terrain LOD 算法的展示名称和简短描述
/// </summary>
struct TerrainLodAlgorithmInfo
{
    TerrainLodAlgorithmId Id{TerrainLodAlgorithmId::ClassicCpuRoam};
    std::string_view Name;
    std::string_view DisplayName;
    std::string_view Description;
};

/// <summary>
/// 描述某个 terrain LOD 算法当前可输出的渲染路径和拓扑能力
/// </summary>
struct TerrainLodAlgorithmCapabilities
{
    bool SupportsCpuMeshOutput{false};
    bool SupportsGpuDrivenRendering{false};
    bool SupportsProceduralIndirectRendering{false};
    bool SupportsSplit{false};
    bool SupportsMerge{false};
    bool SupportsCrackFix{false};
    bool SupportsTopologyValidation{false};
    bool RequiresShaderModel66{false};
    bool RequiresInt64ShaderOps{false};
    bool RequiresInt64Atomics{false};
};

/// <summary>
/// 控制层和 GUI 共享的算法可用状态，不暴露具体图形后端实现
/// </summary>
struct TerrainLodAlgorithmAvailability
{
    bool Available{true};
    std::string UnavailableReason;
};

/// <summary>
/// terrain LOD 算法共享的运行参数，benchmark 和 renderer 使用同一套字段做公平对比
/// </summary>
struct TerrainLodSettings
{
    float TerrainSize{30.0F};
    float HeightScale{4.0F};
    int MaxDepth{14};
    // Classic、DOD 和 GPU ROAM-like 共享像素单位的迟滞阈值。
    float ScreenSpaceSplitThresholdPixels{4.0F};
    float ScreenSpaceMergeThresholdPixels{2.0F};
    // 三种 ROAM 路径共享活动 leaf triangle 硬上限。
    std::size_t TriangleBudget{20000U};
    bool EnableLocalConstraints{true};
    bool EnableTopologyValidation{false};
};

/// <summary>
/// 视锥平面在 TerrainLodViewInput 中的固定顺序
/// </summary>
enum class TerrainLodFrustumPlane
{
    Left,
    Right,
    Bottom,
    Top,
    Near,
    Far,
    Count,
};

/// <summary>
/// renderer 与视点相关 LOD 算法共享的只读视图数据
/// </summary>
struct TerrainLodViewInput
{
    glm::mat4 View{1.0F};
    glm::mat4 Projection{1.0F};
    glm::mat4 ViewProjection{1.0F};
    glm::vec3 CameraPosition{0.0F};
    glm::vec3 CameraForward{0.0F, 0.0F, -1.0F};
    std::array<glm::vec4, static_cast<std::size_t>(TerrainLodFrustumPlane::Count)> FrustumPlanes{}; // 向内法线且内侧平面值非负
    std::uint32_t DrawableWidth{1U};
    std::uint32_t DrawableHeight{1U};
};

/// <summary>
/// 单帧 LOD 构建输入，固定高度图、视图和统一算法参数
/// </summary>
struct TerrainLodBuildInput
{
    const Terrain::HeightMap* HeightMap{nullptr};
    TerrainLodViewInput View;
    TerrainLodSettings Settings;
};

/// <summary>
/// 算法渲染输出模式，区分 CPU mesh、GPU buffer 和 GPU driven 路径
/// </summary>
enum class TerrainLodRenderMode
{
    CpuMesh,
    GpuBuffers,
    GpuIndirect,
    GpuProceduralIndirect,
    DebugOnly,
};

/// <summary>
/// 算法返回的 GPU 资源借用生命周期
/// </summary>
enum class TerrainLodGpuResourceLifetime
{
    None,
    UntilNextBuildOrReset,
};

/// <summary>
/// 数据包内原生 GPU 资源所属的图形 API
/// </summary>
enum class TerrainLodNativeResourceApi
{
    None,
    Direct3D12,
};

/// <summary>
/// 算法输出给 renderer 或 benchmark 的统一渲染数据包
/// </summary>
struct TerrainLodRenderPacket
{
    TerrainLodRenderMode Mode{TerrainLodRenderMode::CpuMesh};
    Terrain::TerrainMeshData CpuMesh;
    std::string StatusMessage;
    std::uint32_t GpuNodeBufferId{0};
    std::uint32_t GpuHeightMapTextureId{0};
    std::uint32_t GpuVertexBufferId{0};
    std::uint32_t GpuIndexBufferId{0};
    std::uint32_t ActiveLeafBufferId{0};
    std::uint32_t IndirectDrawBufferId{0};
    // 原生 API 与下列 uintptr_t 字段共同解释资源类型
    TerrainLodNativeResourceApi NativeResourceApi{TerrainLodNativeResourceApi::None};
    // D3D12 路径借用的顶点资源地址
    std::uintptr_t NativeVertexBuffer{0};
    // D3D12 路径借用的索引资源地址
    std::uintptr_t NativeIndexBuffer{0};
    // 程序化路径借用的活动二分器索引资源地址
    std::uintptr_t NativeActiveLeafBuffer{0};
    // D3D12 间接路径借用的命令参数资源地址
    std::uintptr_t NativeIndirectDrawBuffer{0};
    // 原生顶点视图允许访问的总字节数
    std::size_t GpuVertexBufferCapacityBytes{0};
    // 程序化顶点 SRV 的结构化元素跨度
    std::size_t GpuVertexStrideBytes{0};
    // 原生索引视图允许访问的总字节数
    std::size_t GpuIndexBufferCapacityBytes{0};
    // 活动二分器 SRV 允许访问的总字节数和结构化元素跨度
    std::size_t GpuActiveLeafBufferCapacityBytes{0};
    std::size_t GpuActiveLeafStrideBytes{0};
    // 渲染器必须遵守的借用生命周期
    TerrainLodGpuResourceLifetime GpuResourceLifetime{TerrainLodGpuResourceLifetime::None};
    // 每次算法重建递增，用于识别失效资源
    std::uint64_t GpuResourceGeneration{0};
    std::size_t ActiveLeafCount{0};
    std::size_t ActiveTriangleCount{0};
    std::size_t IndexCount{0};

    /// <summary>
    /// 校验跨 API 资源互斥、容量、步长和借用生命周期是否满足当前渲染模式
    /// </summary>
    [[nodiscard]] bool HasConsistentResourceContract() const
    {
        // OpenGL 对象编号和 D3D12 原生资源不能混合解释
        const bool hasGpuResourceIds =
            GpuNodeBufferId != 0U ||
            GpuHeightMapTextureId != 0U ||
            GpuVertexBufferId != 0U ||
            GpuIndexBufferId != 0U ||
            ActiveLeafBufferId != 0U ||
            IndirectDrawBufferId != 0U;
        const bool hasNativeGpuResources =
            NativeVertexBuffer != 0U ||
            NativeIndexBuffer != 0U ||
            NativeActiveLeafBuffer != 0U ||
            NativeIndirectDrawBuffer != 0U;

        if (Mode == TerrainLodRenderMode::CpuMesh || Mode == TerrainLodRenderMode::DebugOnly)
        {
            // 非 GPU 模式禁止携带任何需要生命周期管理的资源
            return !hasGpuResourceIds && !hasNativeGpuResources &&
                   NativeResourceApi == TerrainLodNativeResourceApi::None &&
                   GpuResourceLifetime == TerrainLodGpuResourceLifetime::None &&
                   GpuResourceGeneration == 0U;
        }

        if (Mode == TerrainLodRenderMode::GpuProceduralIndirect)
        {
            const bool hasValidStrides =
                GpuVertexStrideBytes > 0U &&
                GpuActiveLeafStrideBytes > 0U;
            const std::size_t vertexCapacity = hasValidStrides
                ? GpuVertexBufferCapacityBytes / GpuVertexStrideBytes
                : 0U;
            const std::size_t activeLeafCapacity = hasValidStrides
                ? GpuActiveLeafBufferCapacityBytes / GpuActiveLeafStrideBytes
                : 0U;
            const bool hasRequiredCapacity =
                ActiveTriangleCount <= vertexCapacity / 3U &&
                ActiveLeafCount <= activeLeafCapacity;

            return NativeResourceApi == TerrainLodNativeResourceApi::Direct3D12 &&
                   NativeVertexBuffer != 0U &&
                   NativeIndexBuffer == 0U &&
                   NativeActiveLeafBuffer != 0U &&
                   NativeIndirectDrawBuffer != 0U &&
                   !hasGpuResourceIds &&
                   hasValidStrides &&
                   hasRequiredCapacity &&
                   ActiveLeafCount > 0U &&
                   ActiveTriangleCount > 0U &&
                   IndexCount == 0U &&
                   GpuResourceLifetime == TerrainLodGpuResourceLifetime::UntilNextBuildOrReset &&
                   GpuResourceGeneration > 0U;
        }

        // OpenGL 路径通过非零对象编号表达资源有效性
        const bool hasOpenGlDrawResources =
            NativeResourceApi == TerrainLodNativeResourceApi::None &&
            !hasNativeGpuResources &&
            GpuVertexBufferId != 0U && GpuIndexBufferId != 0U;
        // D3D12 视图除资源指针外还必须提供非零容量
        const bool hasD3D12DrawResources =
            NativeResourceApi == TerrainLodNativeResourceApi::Direct3D12 &&
            !hasGpuResourceIds &&
            NativeVertexBuffer != 0U && NativeIndexBuffer != 0U &&
            GpuVertexBufferCapacityBytes > 0U && GpuIndexBufferCapacityBytes > 0U;
        const bool hasNoProceduralMetadata =
            NativeActiveLeafBuffer == 0U &&
            GpuVertexStrideBytes == 0U &&
            GpuActiveLeafBufferCapacityBytes == 0U &&
            GpuActiveLeafStrideBytes == 0U;
        const bool hasRequiredDrawResources =
            (hasOpenGlDrawResources || hasD3D12DrawResources) &&
            IndexCount > 0U &&
            ActiveTriangleCount > 0U;
        // 只有间接模式强制要求额外命令参数缓冲
        const bool hasRequiredIndirectResource =
            Mode != TerrainLodRenderMode::GpuIndirect ||
            (NativeResourceApi == TerrainLodNativeResourceApi::Direct3D12
                ? NativeIndirectDrawBuffer != 0U
                : IndirectDrawBufferId != 0U);

        return hasRequiredDrawResources &&
               hasRequiredIndirectResource &&
               hasNoProceduralMetadata &&
               GpuResourceLifetime == TerrainLodGpuResourceLifetime::UntilNextBuildOrReset &&
               GpuResourceGeneration > 0U;
    }
};

/// <summary>
/// 跨 Classic / Data-Oriented / GPU / CBT 版本共享的统计字段，用于 UI 展示、回归测试和 CSV 输出
/// </summary>
struct TerrainLodStats
{
    std::size_t ActiveTriangleCount{0};
    std::size_t ActiveNodeCount{0};
    std::size_t OriginalTriangleCount{0};
    std::size_t SubdividedTriangleCount{0};
    std::size_t RebuiltTriangleCount{0};
    std::size_t ActiveSplitCount{0};
    std::size_t SplitCount{0};
    std::size_t ForcedSplitCount{0};
    std::size_t MergeCount{0};
    std::size_t CrackRiskCount{0};
    std::size_t ConstraintPassCount{0};
    std::size_t CandidatePeakCount{0};
    std::size_t RejectedSplitCount{0};
    std::size_t BudgetRejectedSplitCount{0};
    std::size_t RejectedMergeCount{0};
    std::size_t TjunctionCount{0};
    std::size_t InvalidNeighborCount{0};
    std::size_t InvalidTopologyCount{0};
    std::size_t CpuGpuUploadBytes{0};
    std::size_t CpuGpuReadbackBytes{0};
    // CpuWorkerCount 表示本次 CPU LOD build 的实际并行宽度
    std::size_t CpuWorkerCount{0};
    float CpuUpdateMilliseconds{0.0F};
    // CpuUtilizationPercent 按单核 100% 口径记录进程 CPU 占用
    float CpuUtilizationPercent{0.0F};
    float CpuErrorEvalMilliseconds{0.0F};
    float CpuDecisionMilliseconds{0.0F};
    float CpuTopologyMilliseconds{0.0F};
    float CpuCollectMilliseconds{0.0F};
    float CpuMeshBuildMilliseconds{0.0F};
    float CpuUploadMilliseconds{0.0F};
    float GpuComputeMilliseconds{0.0F};
    float GpuSnapshotBuildMilliseconds{0.0F};
    float GpuBufferAllocationMilliseconds{0.0F};
    float GpuDispatchWallMilliseconds{0.0F};
    float GpuQueryWaitMilliseconds{0.0F};
    float GpuReadbackWaitMilliseconds{0.0F};
    float RenderMilliseconds{0.0F};
    float SplitMilliseconds{0.0F};
    float MergeMilliseconds{0.0F};
    float EmitMilliseconds{0.0F};
    float ValidateMilliseconds{0.0F};
    int MaxActiveDepth{0};
};

/// <summary>
/// 地形 LOD 算法的统一边界，所有 CPU 和 GPU 实现都通过它接入 renderer 和 benchmark
/// </summary>
class ITerrainLodAlgorithm
{
public:
    virtual ~ITerrainLodAlgorithm() = default;

    [[nodiscard]] virtual TerrainLodAlgorithmInfo Info() const = 0;
    [[nodiscard]] virtual TerrainLodAlgorithmCapabilities Capabilities() const = 0;

    /// <summary>
    /// 根据固定输入构建当前帧统一渲染包，失败时通过 errorMessage 暴露可诊断原因
    /// </summary>
    [[nodiscard]] virtual bool BuildRenderData(
        const TerrainLodBuildInput& input,
        TerrainLodRenderPacket& outPacket,
        std::string* errorMessage) = 0;

    [[nodiscard]] virtual const TerrainLodStats& Stats() const = 0;
    virtual void Reset() = 0;
};
} // 命名空间 ParallelRoam::Algorithms
