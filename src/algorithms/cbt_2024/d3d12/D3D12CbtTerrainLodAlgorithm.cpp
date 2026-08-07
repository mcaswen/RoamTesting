#include "algorithms/cbt_2024/d3d12/D3D12CbtTerrainLodAlgorithm.h"

#include "algorithms/cbt_2024/Cbt2024Support.h"
#include "algorithms/cbt_2024/CbtBisectorTopology.h"
#include "render/D3D12GraphicsBackend.h"
#include "tools/PerformanceTimer.h"

#include <d3d12.h>
#include <wrl/client.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <type_traits>

namespace ParallelRoam::Algorithms::Cbt2024::D3D12
{
/// <summary>
/// CBT 基础拓扑及其程序化绘制适配资源
/// </summary>
struct D3D12CbtTerrainState
{
    // CPU 真值和默认堆资源状态共享同一构建函数，adapter 不复制拓扑常量
    CbtBaseTopology Topology{BuildSquareCbtBaseTopology(CbtOccupancyCapacity::Capacity128K)};
    Microsoft::WRL::ComPtr<ID3D12Resource> ActiveBisectors;
    Microsoft::WRL::ComPtr<ID3D12Resource> Vertices;
    Microsoft::WRL::ComPtr<ID3D12Resource> IndirectArguments;
    // 上传堆保持持久映射，只有资源重建时写入，不在普通帧重复 Map
    std::uint8_t* MappedActiveBisectors{nullptr};
    std::uint8_t* MappedVertices{nullptr};
    std::uint8_t* MappedIndirectArguments{nullptr};
    std::size_t ActiveCapacityBytes{0U};
    std::size_t VertexCapacityBytes{0U};
    // 尺寸参数是基础几何缓存键，Generation 是 renderer 描述符缓存键
    float TerrainSize{0.0F};
    float HeightScale{0.0F};
    std::uint64_t Generation{0};

    ~D3D12CbtTerrainState()
    {
        // Unmap 只与成功 Map 的资源配对，允许部分初始化失败后安全析构
        if (ActiveBisectors != nullptr && MappedActiveBisectors != nullptr)
        {
            ActiveBisectors->Unmap(0, nullptr);
        }
        if (Vertices != nullptr && MappedVertices != nullptr)
        {
            Vertices->Unmap(0, nullptr);
        }
        if (IndirectArguments != nullptr && MappedIndirectArguments != nullptr)
        {
            IndirectArguments->Unmap(0, nullptr);
        }
    }
};

namespace
{
constexpr std::size_t ActiveBisectorCount = CbtBaseBisectorCount;
constexpr std::size_t VerticesPerBisector = 3U;
constexpr std::size_t ActiveBytes = ActiveBisectorCount * sizeof(std::uint32_t);
constexpr std::size_t IndirectBytes = sizeof(D3D12_DRAW_ARGUMENTS);

// StructuredBuffer 按固定字节布局读取 TerrainMeshVertex，GLM 对齐选项变化时必须在编译期失败
// 每个 offset 对应 CbtProceduralTerrain.hlsl 中同名字段的自然布局
static_assert(std::is_standard_layout_v<Terrain::TerrainMeshVertex>);
static_assert(sizeof(Terrain::TerrainMeshVertex) == 52U);
static_assert(offsetof(Terrain::TerrainMeshVertex, Position) == 0U);
static_assert(offsetof(Terrain::TerrainMeshVertex, Normal) == 12U);
static_assert(offsetof(Terrain::TerrainMeshVertex, TexCoord) == 24U);
static_assert(offsetof(Terrain::TerrainMeshVertex, Height) == 32U);
static_assert(offsetof(Terrain::TerrainMeshVertex, DebugColor) == 36U);
static_assert(offsetof(Terrain::TerrainMeshVertex, DebugHighlight) == 48U);
static_assert(sizeof(CbtDrawArguments) == sizeof(D3D12_DRAW_ARGUMENTS));

// 所有验证资源共享相同 heap 属性，集中构造可避免某个 buffer 错用默认堆
// node mask 固定为单适配器设备，与后端创建设备时的约定一致
D3D12_HEAP_PROPERTIES UploadHeapProperties()
{
    // 上传堆允许 CPU 持久写入，并以 GENERIC_READ 同时服务 SRV 和间接参数读取
    D3D12_HEAP_PROPERTIES properties{};
    properties.Type = D3D12_HEAP_TYPE_UPLOAD;
    properties.CreationNodeMask = 1;
    properties.VisibleNodeMask = 1;
    return properties;
}

// 只构造 byte-address 容量意义上的通用 buffer 描述
// 元素格式和数量在 renderer 创建 SRV 时再由渲染包元数据决定
D3D12_RESOURCE_DESC BufferDescription(std::size_t bytes)
{
    // D3D12 buffer 必须使用单 mip、单数组层和 ROW_MAJOR 布局
    D3D12_RESOURCE_DESC description{};
    description.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    description.Width = bytes;
    description.Height = 1;
    description.DepthOrArraySize = 1;
    description.MipLevels = 1;
    description.SampleDesc.Count = 1;
    description.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    return description;
}

void SetError(std::string* errorMessage, const std::string& message)
{
    if (errorMessage != nullptr)
    {
        *errorMessage = message;
    }
}

// 返回 true 时 resource 和 mappedMemory 必须同时有效
// 返回 false 时调用方可以保留其他已经创建成功的资源并在下一帧重试
bool CreateMappedBuffer(
    ID3D12Device* device,
    std::size_t bytes,
    Microsoft::WRL::ComPtr<ID3D12Resource>& resource,
    std::uint8_t*& mappedMemory,
    std::string* errorMessage)
{
    // 资源创建和映射组成一个原子初始化步骤，失败时不向状态对象泄漏半成品指针
    const D3D12_HEAP_PROPERTIES heap = UploadHeapProperties();
    const D3D12_RESOURCE_DESC description = BufferDescription(bytes);
    const HRESULT result = device->CreateCommittedResource(
        &heap,
        D3D12_HEAP_FLAG_NONE,
        &description,
        D3D12_RESOURCE_STATE_GENERIC_READ,
        nullptr,
        IID_PPV_ARGS(&resource));
    if (FAILED(result))
    {
        SetError(errorMessage, "Failed to allocate CBT procedural upload buffer");
        return false;
    }

    // 空读取范围声明 CPU 不会读取旧内容，驱动无需为 Map 准备回读同步
    const D3D12_RANGE noReadRange{0, 0};
    void* mapped = nullptr;
    if (FAILED(resource->Map(0, &noReadRange, &mapped)))
    {
        resource.Reset();
        SetError(errorMessage, "Failed to map CBT procedural upload buffer");
        return false;
    }
    mappedMemory = static_cast<std::uint8_t*>(mapped);
    return true;
}

// 将高度图 UV 转换为程序化物理槽位使用的完整 terrain 顶点
// 颜色参数只服务活动槽位映射的视觉诊断，不参与拓扑含义
Terrain::TerrainMeshVertex MakeVertex(
    const Terrain::HeightMap& heightMap,
    float u,
    float v,
    float terrainSize,
    float heightScale,
    const glm::vec3& debugColor)
{
    Terrain::TerrainMeshVertex vertex{};
    // 高度值保留归一化采样结果，Position.y 才应用运行时高度缩放
    vertex.Height = heightMap.SampleBilinear(u, v);
    // UV 中心平移到世界原点，使人工网格与现有规则地形占用同一范围
    vertex.Position = glm::vec3{
        (u - 0.5F) * terrainSize,
        vertex.Height * heightScale,
        (v - 0.5F) * terrainSize};
    vertex.TexCoord = glm::vec2{u, v};
    vertex.DebugColor = debugColor;
    vertex.DebugHighlight = 0.75F;
    return vertex;
}

// 三个顶点共享面法线，当前验证不引入平滑法线造成的额外变量
// 参数按最终绕序传入，函数不负责交换顶点或修复背面
void SetTriangleNormal(
    Terrain::TerrainMeshVertex& a,
    Terrain::TerrainMeshVertex& b,
    Terrain::TerrainMeshVertex& c)
{
    // 人工三角形仍走正式 terrain pixel shader，因此必须提供稳定世界空间法线
    const glm::vec3 normal = glm::cross(b.Position - a.Position, c.Position - a.Position);
    // 高度和尺寸退化时回退向上法线，防止 normalize(0) 产生无效光照输入
    const float lengthSquared = glm::dot(normal, normal);
    const glm::vec3 safeNormal = lengthSquared > 0.000001F
        ? glm::normalize(normal)
        : glm::vec3{0.0F, 1.0F, 0.0F};
    a.Normal = safeNormal;
    b.Normal = safeNormal;
    c.Normal = safeNormal;
}

// 该步骤只保证容量和映射存在，不写入任何一帧可见数据
// 数据发布由 RebuildProceduralResources 在队列同步后统一完成
bool EnsureProceduralBuffers(
    D3D12CbtTerrainState& state,
    ID3D12Device* device,
    std::string* errorMessage)
{
    // 持久映射上传堆只负责把基础拓扑桥接到现有程序化绘制协议
    // 分项创建允许重试复用前面已经成功的资源，不重复分配稳定槽位
    if (state.ActiveBisectors == nullptr &&
        !CreateMappedBuffer(
            device,
            ActiveBytes,
            state.ActiveBisectors,
            state.MappedActiveBisectors,
            errorMessage))
    {
        return false;
    }
    if (state.Vertices == nullptr &&
        !CreateMappedBuffer(
            device,
            state.VertexCapacityBytes,
            state.Vertices,
            state.MappedVertices,
            errorMessage))
    {
        return false;
    }
    return state.IndirectArguments != nullptr ||
           CreateMappedBuffer(
               device,
               IndirectBytes,
               state.IndirectArguments,
               state.MappedIndirectArguments,
               errorMessage);
}

// 写入内容刻意保持最小规模，但完整覆盖活动索引、物理顶点和间接参数协议
// 函数要求三个 mapped 指针已经由 EnsureProceduralBuffers 建立
void WriteProceduralInputs(
    D3D12CbtTerrainState& state,
    const TerrainLodBuildInput& input)
{
    const CbtBaseTopology& topology = state.Topology;
    std::memcpy(state.MappedActiveBisectors, topology.ActiveIndices.data(), ActiveBytes);

    // vertex shader 直接用物理槽位寻址，因此顶点缓冲保留完整物理范围
    // 动态前缀当前为空，六个基础二分器写入容量尾部对应的三顶点块
    std::memset(state.MappedVertices, 0, state.VertexCapacityBytes);
    auto* vertices = reinterpret_cast<Terrain::TerrainMeshVertex*>(state.MappedVertices);
    const std::array<glm::vec3, CbtBaseBisectorCount> colors{{
        {0.08F, 0.72F, 0.62F},
        {0.10F, 0.52F, 0.88F},
        {0.45F, 0.76F, 0.24F},
        {1.0F, 0.58F, 0.12F},
        {0.90F, 0.28F, 0.24F},
        {0.70F, 0.34F, 0.82F},
    }};
    for (std::size_t bisector = 0U; bisector < CbtBaseBisectorCount; ++bisector)
    {
        // active ordinal 与 physicalSlot 不相等，shader 会再次执行同一层间接寻址
        // 这里按物理槽写入可验证尾部基础槽位没有被误当成紧凑序号
        const std::size_t physicalSlot = topology.ActiveIndices[bisector];
        Terrain::TerrainMeshVertex* triangle = vertices + physicalSlot * VerticesPerBisector;
        for (std::size_t localVertex = 0U; localVertex < VerticesPerBisector; ++localVertex)
        {
            const CbtBaseControlPoint& point = topology.ControlPoints[bisector * VerticesPerBisector + localVertex];
            triangle[localVertex] = MakeVertex(
                *input.HeightMap,
                point.U,
                point.V,
                input.Settings.TerrainSize,
                input.Settings.HeightScale,
                colors[bisector]);
        }
        SetTriangleNormal(triangle[0], triangle[1], triangle[2]);
    }

    // 间接参数与正式拓扑命令使用同一跨平台二进制布局
    const CbtDrawArguments& sourceArguments = topology.DrawCommands[0];
    const D3D12_DRAW_ARGUMENTS drawArguments{
        sourceArguments.VertexCountPerInstance,
        sourceArguments.InstanceCount,
        sourceArguments.StartVertexLocation,
        sourceArguments.StartInstanceLocation};
    // 三个上传资源在同一次 Generation 发布，renderer 不会观察到混合版本
    std::memcpy(state.MappedIndirectArguments, &drawArguments, IndirectBytes);
}

// 资源重建是唯一允许修改共享上传内存的入口
// 成功返回前会发布新的 Generation，失败时保持旧版本号
bool RebuildProceduralResources(
    D3D12CbtTerrainState& state,
    Render::D3D12GraphicsBackend& backend,
    const TerrainLodBuildInput& input,
    std::string* errorMessage)
{
    // adapter 在帧内只创建上传堆资源，正式默认堆状态由独立拓扑路径管理
    if (state.VertexCapacityBytes == 0U)
    {
        state.ActiveCapacityBytes = ActiveBytes;
        // 每个物理槽固定预留三个 terrain 顶点，动态槽位将由后续几何 pass 原位写入
        // 容量包含六个基础槽，最后一个 physicalSlot 的三顶点块必须完整落在缓冲内
        state.VertexCapacityBytes =
            static_cast<std::size_t>(state.Topology.Layout.TotalElementCount) *
            VerticesPerBisector * sizeof(Terrain::TerrainMeshVertex);
    }

    // 三个上传资源被所有帧借用，改写前必须等待旧描述符和间接命令全部完成
    if (state.Generation > 0U)
    {
        backend.WaitForGpuIdle();
    }
    if (!EnsureProceduralBuffers(state, backend.Device(), errorMessage))
    {
        return false;
    }

    // 数据完全写入后再递增 Generation，逐帧描述符据此判断是否需要重写
    WriteProceduralInputs(state, input);
    state.TerrainSize = input.Settings.TerrainSize;
    state.HeightScale = input.Settings.HeightScale;
    ++state.Generation;
    return true;
}

// 渲染包只描述借用资源和边界元数据，不转移 COM 所有权
// 所有字段必须能独立通过 HasConsistentResourceContract 校验
void FillRenderPacket(
    const D3D12CbtTerrainState& state,
    TerrainLodRenderPacket& outPacket)
{
    outPacket.Mode = TerrainLodRenderMode::GpuProceduralIndirect;
    outPacket.StatusMessage = "CBT 2024 base bisector topology";
    // uintptr_t 只跨越统一算法接口传递，资源引用仍由 D3D12CbtProceduralState 持有
    outPacket.NativeResourceApi = TerrainLodNativeResourceApi::Direct3D12;
    outPacket.NativeVertexBuffer = reinterpret_cast<std::uintptr_t>(state.Vertices.Get());
    outPacket.NativeActiveLeafBuffer = reinterpret_cast<std::uintptr_t>(state.ActiveBisectors.Get());
    outPacket.NativeIndirectDrawBuffer = reinterpret_cast<std::uintptr_t>(state.IndirectArguments.Get());
    // 容量和 stride 共同限定 StructuredBuffer 可见范围，防止 shader 越过物理槽位
    outPacket.GpuVertexBufferCapacityBytes = state.VertexCapacityBytes;
    outPacket.GpuVertexStrideBytes = sizeof(Terrain::TerrainMeshVertex);
    outPacket.GpuActiveLeafBufferCapacityBytes = state.ActiveCapacityBytes;
    outPacket.GpuActiveLeafStrideBytes = sizeof(std::uint32_t);
    // renderer 只能在下一次 Build 或 Reset 前使用这些借用指针
    // Generation 变化时所有帧的 SRV 描述符都必须按各自 fence 节奏更新
    outPacket.GpuResourceLifetime = TerrainLodGpuResourceLifetime::UntilNextBuildOrReset;
    outPacket.GpuResourceGeneration = state.Generation;
    outPacket.ActiveLeafCount = ActiveBisectorCount;
    outPacket.ActiveTriangleCount = ActiveBisectorCount;
}
} // namespace

D3D12CbtTerrainLodAlgorithm::D3D12CbtTerrainLodAlgorithm(Render::D3D12GraphicsBackend& backend)
    : _backend(&backend),
      _state(std::make_unique<D3D12CbtTerrainState>())
{
}

D3D12CbtTerrainLodAlgorithm::~D3D12CbtTerrainLodAlgorithm() = default;

TerrainLodAlgorithmInfo D3D12CbtTerrainLodAlgorithm::Info() const
{
    return TerrainLodAlgorithmInfo{
        TerrainLodAlgorithmId::Cbt2024,
        "cbt-2024-base-topology",
        "CBT 2024（基础拓扑）",
        "六个基础二分器驱动的 CBT 程序化间接绘制",
    };
}

TerrainLodAlgorithmCapabilities D3D12CbtTerrainLodAlgorithm::Capabilities() const
{
    return TerrainLodAlgorithmCapabilities{
        .SupportsCpuMeshOutput = false,
        .SupportsGpuDrivenRendering = true,
        .SupportsProceduralIndirectRendering = true,
        .SupportsSplit = false,
        .SupportsMerge = false,
        .SupportsCrackFix = false,
        .SupportsTopologyValidation = false,
        .RequiresShaderModel66 = true,
        .RequiresInt64ShaderOps = true,
        .RequiresInt64Atomics = true,
    };
}

// BuildRenderData 当前绘制合法基础拓扑，不执行 split 或 merge
// 后续更新 pass 将复用同一拓扑状态和渲染包协议
bool D3D12CbtTerrainLodAlgorithm::BuildRenderData(
    const TerrainLodBuildInput& input,
    TerrainLodRenderPacket& outPacket,
    std::string* errorMessage)
{
    Tools::PerformanceTimer buildTimer;
    // 每次构建先清空输出，失败路径不能遗留上一帧可绘制资源
    outPacket = {};
    _stats = {};

    // 设备存在性和能力要求分开报告，便于定位初始化失败或硬件能力不足
    if (_backend == nullptr || _backend->Device() == nullptr)
    {
        SetError(errorMessage, "CBT 2024 requires an initialized D3D12 device");
        return false;
    }
    const Cbt2024Availability availability = QueryCbt2024Availability(*_backend);
    if (!availability.Available)
    {
        SetError(errorMessage, availability.UnavailableReason);
        return false;
    }
    if (input.HeightMap == nullptr || !input.HeightMap->IsValid())
    {
        SetError(errorMessage, "CBT procedural validation requires a valid height map");
        return false;
    }

    // 相机变化不影响基础几何，只有首次构建和地形尺度变化才需要队列同步重写
    const bool requiresResourceBuild =
        _state->Generation == 0U ||
        _state->TerrainSize != input.Settings.TerrainSize ||
        _state->HeightScale != input.Settings.HeightScale;
    if (requiresResourceBuild)
    {
        if (!RebuildProceduralResources(*_state, *_backend, input, errorMessage))
        {
            return false;
        }
        _stats.CpuGpuUploadBytes = ActiveBytes + _state->VertexCapacityBytes + IndirectBytes;
    }

    // 六个活动三角形来自两个根面的半边-面中心分解
    _stats.ActiveTriangleCount = ActiveBisectorCount;
    _stats.ActiveNodeCount = ActiveBisectorCount;
    _stats.OriginalTriangleCount = ActiveBisectorCount;
    _stats.MaxActiveDepth = 0;
    _stats.CpuUpdateMilliseconds = buildTimer.Stop();

    FillRenderPacket(*_state, outPacket);
    // 统一契约检查是返回成功前的最后门禁，避免 renderer 接收不完整资源组合
    return outPacket.HasConsistentResourceContract();
}

const TerrainLodStats& D3D12CbtTerrainLodAlgorithm::Stats() const
{
    return _stats;
}

void D3D12CbtTerrainLodAlgorithm::Reset()
{
    // 固定资源可跨 Reset 复用，统计必须清零以免污染下一轮实验采样
    _stats = {};
}
} // namespace ParallelRoam::Algorithms::Cbt2024::D3D12
