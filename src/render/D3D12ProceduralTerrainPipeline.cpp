#include "render/D3D12ProceduralTerrainPipeline.h"

#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>
#include <limits>
#include <vector>

namespace ParallelRoam::Render
{
namespace
{
// 内部 helper 统一遵守可空 errorMessage 约定
// 调用方只处理 bool，不需要理解 HRESULT 或文件流状态
void SetError(std::string* errorMessage, const std::string& message)
{
    if (errorMessage != nullptr)
    {
        *errorMessage = message;
    }
}

// shader 文件一次性载入内存，PSO 创建完成后 vector 即可释放
// 失败消息必须区分打开、空文件和短读三种资源问题
std::vector<std::uint8_t> ReadBinaryFile(const std::filesystem::path& path, std::string* errorMessage)
{
    // PSO 初始化依赖完整 DXIL blob，打开失败必须保留具体文件路径
    std::ifstream stream{path, std::ios::binary | std::ios::ate};
    if (!stream)
    {
        SetError(errorMessage, "Failed to open D3D12 shader: " + path.string());
        return {};
    }
    // 先定位末尾获取长度，避免逐块增长 vector 并隐藏空 shader 产物
    const std::streamsize size = stream.tellg();
    if (size <= 0)
    {
        SetError(errorMessage, "D3D12 shader is empty: " + path.string());
        return {};
    }
    // 单次分配和读取保证返回数据可直接作为 D3D12_SHADER_BYTECODE 使用
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
    stream.seekg(0, std::ios::beg);
    if (!stream.read(reinterpret_cast<char*>(bytes.data()), size))
    {
        SetError(errorMessage, "Failed to read D3D12 shader: " + path.string());
        return {};
    }
    return bytes;
}

// 状态构造函数显式填写本管线依赖的字段
// 未使用字段保持 D3D12 零初始化语义
D3D12_BLEND_DESC OpaqueBlendDescription()
{
    // terrain pixel shader 输出直接覆盖颜色目标，不参与透明混合
    D3D12_BLEND_DESC description{};
    description.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    return description;
}

// solid 与 wireframe 只通过 fillMode 分化，其他光栅约定必须一致
D3D12_RASTERIZER_DESC RasterizerDescription(D3D12_FILL_MODE fillMode)
{
    // 人工槽位沿用正式地形逆时针绕序，背面剔除可直接发现索引映射错误
    D3D12_RASTERIZER_DESC description{};
    description.FillMode = fillMode;
    description.CullMode = D3D12_CULL_MODE_BACK;
    description.FrontCounterClockwise = TRUE;
    description.DepthClipEnable = TRUE;
    return description;
}

// 独立函数避免两个 PSO 分支出现深度状态漂移
D3D12_DEPTH_STENCIL_DESC DepthStencilDescription()
{
    // 程序化路径与普通 terrain 共用深度缓冲，必须保持相同 LESS 写入约定
    D3D12_DEPTH_STENCIL_DESC description{};
    description.DepthEnable = TRUE;
    description.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
    description.DepthFunc = D3D12_COMPARISON_FUNC_LESS;
    return description;
}
} // namespace

D3D12ProceduralTerrainPipeline::~D3D12ProceduralTerrainPipeline()
{
    Shutdown();
}

// 初始化建立稳定管线对象和每帧描述符槽位，不绑定算法资源
// 算法资源可能每次 Build 改变，由 ConfigureResourceDescriptors 延迟写入
bool D3D12ProceduralTerrainPipeline::Initialize(
    D3D12GraphicsBackend& backend,
    std::string* errorMessage)
{
    // Initialize 支持失败后重试，先释放旧对象避免描述符槽位重复占用
    Shutdown();
    _backend = &backend;
    // 创建顺序遵循依赖关系，任一步失败都由 Shutdown 回滚已经完成的部分
    if (!CreateRootSignature(errorMessage) ||
        !CreatePipelineStates(errorMessage) ||
        !CreateCommandSignature(errorMessage) ||
        !AllocateFrameDescriptors(errorMessage))
    {
        Shutdown();
        return false;
    }
    return true;
}

// Shutdown 可重复调用，既服务显式 renderer 清理也服务析构回退
// backend 指针清空后再次调用不会重复归还描述符
void D3D12ProceduralTerrainPipeline::Shutdown()
{
    // SRV 槽位属于后端分配器，必须在 backend 仍存活时显式归还
    if (_backend != nullptr)
    {
        for (std::uint32_t frameIndex = 0; frameIndex < D3D12GraphicsBackend::FrameCount; ++frameIndex)
        {
            _backend->ReleaseSrvDescriptor(_activeElementSrvs[frameIndex]);
            _backend->ReleaseSrvDescriptor(_vertexSrvs[frameIndex]);
        }
    }
    // 先归还外部分配的描述符，再释放引用这些槽位的管线对象
    _descriptorGenerations = {};
    _drawCommandSignature.Reset();
    _wireframePipelineState.Reset();
    _fillPipelineState.Reset();
    _rootSignature.Reset();
    _backend = nullptr;
}

void D3D12ProceduralTerrainPipeline::InvalidateResourceDescriptors()
{
    // 零版本表示每个交换链帧下次使用时都必须重新创建 SRV
    _descriptorGenerations = {};
}

// 该函数是算法借用资源进入图形管线的唯一入口
// 它不记录命令，只为当前 frameIndex 建立稳定 SRV 解释
bool D3D12ProceduralTerrainPipeline::ConfigureResourceDescriptors(
    std::uint32_t frameIndex,
    ID3D12Resource* vertexBuffer,
    std::size_t vertexCapacityBytes,
    std::size_t vertexStrideBytes,
    ID3D12Resource* activeElementBuffer,
    std::size_t activeElementCapacityBytes,
    std::size_t activeElementStrideBytes,
    std::uint64_t resourceGeneration,
    std::string* errorMessage)
{
    // backend 在进入 Render 前已等待当前 frame fence，因此只允许改写该帧槽位
    if (_backend == nullptr || frameIndex >= D3D12GraphicsBackend::FrameCount)
    {
        SetError(errorMessage, "D3D12 procedural descriptor frame index is invalid");
        return false;
    }
    // 同一资源版本轮转回该帧时复用现有描述符，避免每帧重复设备调用
    if (_descriptorGenerations[frameIndex] == resourceGeneration)
    {
        return true;
    }
    // StructuredBuffer 容量必须是 stride 的整数倍，否则最后一个元素会越界
    if (vertexBuffer == nullptr || activeElementBuffer == nullptr ||
        vertexStrideBytes == 0U || activeElementStrideBytes == 0U ||
        vertexCapacityBytes % vertexStrideBytes != 0U ||
        activeElementCapacityBytes % activeElementStrideBytes != 0U)
    {
        SetError(errorMessage, "D3D12 procedural SRV metadata is incomplete");
        return false;
    }

    // D3D12 描述符字段使用 UINT，先在 size_t 中计算再检查收窄范围
    const std::size_t vertexCount = vertexCapacityBytes / vertexStrideBytes;
    const std::size_t activeElementCount = activeElementCapacityBytes / activeElementStrideBytes;
    if (vertexStrideBytes > std::numeric_limits<UINT>::max() ||
        activeElementStrideBytes > std::numeric_limits<UINT>::max() ||
        vertexCount > std::numeric_limits<UINT>::max() ||
        activeElementCount > std::numeric_limits<UINT>::max())
    {
        SetError(errorMessage, "D3D12 procedural SRV exceeds descriptor limits");
        return false;
    }

    // t1 保存活动序号到物理槽位的映射，元素格式由 StructureByteStride 定义
    D3D12_SHADER_RESOURCE_VIEW_DESC activeElementDescription{};
    activeElementDescription.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING; // 保持标量读取默认映射
    activeElementDescription.Format = DXGI_FORMAT_UNKNOWN; // StructuredBuffer 由 stride 定义格式
    activeElementDescription.ViewDimension = D3D12_SRV_DIMENSION_BUFFER; // 活动表是一维结构化缓冲
    activeElementDescription.Buffer.NumElements = static_cast<UINT>(activeElementCount); // 覆盖完整预留容量
    activeElementDescription.Buffer.StructureByteStride = static_cast<UINT>(activeElementStrideBytes); // 当前为 uint 槽位
    _backend->Device()->CreateShaderResourceView(
        activeElementBuffer,
        &activeElementDescription,
        _activeElementSrvs[frameIndex].Cpu);

    // t2 保存按物理槽位连续排列的三顶点数据，不能复用 t1 的元素跨度
    D3D12_SHADER_RESOURCE_VIEW_DESC vertexDescription{};
    vertexDescription.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING; // 保持结构化字段原始分量
    vertexDescription.Format = DXGI_FORMAT_UNKNOWN; // 顶点记录由 HLSL 结构解释
    vertexDescription.ViewDimension = D3D12_SRV_DIMENSION_BUFFER; // 无 IA 的顶点数据源
    vertexDescription.Buffer.NumElements = static_cast<UINT>(vertexCount); // 三顶点物理槽位总数
    vertexDescription.Buffer.StructureByteStride = static_cast<UINT>(vertexStrideBytes); // TerrainMeshVertex 步长
    _backend->Device()->CreateShaderResourceView(
        vertexBuffer,
        &vertexDescription,
        _vertexSrvs[frameIndex].Cpu);
    // 两个 SRV 都成功写入后才发布版本，失败重试不会误判为已配置
    _descriptorGenerations[frameIndex] = resourceGeneration;
    return true;
}

// RecordDraw 假设当前帧描述符已经配置且 indirectBuffer 满足 DRAW 布局
// 函数不修改资源状态，算法和 renderer 必须提前保证读取状态可用
void D3D12ProceduralTerrainPipeline::RecordDraw(
    ID3D12GraphicsCommandList* commandList,
    std::uint32_t frameIndex,
    D3D12_GPU_VIRTUAL_ADDRESS constantBufferAddress,
    D3D12_GPU_DESCRIPTOR_HANDLE textureSrv,
    ID3D12Resource* indirectBuffer,
    bool wireframe) const
{
    // 根参数顺序固定为 b0、t0、t1、t2，与独立 root signature 完全一致
    commandList->SetPipelineState(wireframe ? _wireframePipelineState.Get() : _fillPipelineState.Get());
    commandList->SetGraphicsRootSignature(_rootSignature.Get());
    commandList->SetGraphicsRootConstantBufferView(0, constantBufferAddress);
    commandList->SetGraphicsRootDescriptorTable(1, textureSrv);
    commandList->SetGraphicsRootDescriptorTable(2, _activeElementSrvs[frameIndex].Gpu);
    commandList->SetGraphicsRootDescriptorTable(3, _vertexSrvs[frameIndex].Gpu);
    commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    // 当前命令缓冲只含一条 DRAW，不使用额外 count buffer
    commandList->ExecuteIndirect(_drawCommandSignature.Get(), 1, indirectBuffer, 0, nullptr, 0);
}

// Ready 只表示持久管线对象完整，不代表某一帧算法资源已经绑定
// solid 和 wireframe 都必须存在，运行时切换模式不能触发空 PSO
bool D3D12ProceduralTerrainPipeline::IsReady() const
{
    return _rootSignature != nullptr &&
           _fillPipelineState != nullptr &&
           _wireframePipelineState != nullptr &&
           _drawCommandSignature != nullptr;
}

// 根签名专门描述无 IA 顶点输入的 terrain 绘制协议
// 与普通 terrain root signature 分离可避免破坏现有 GPU ROAM-like 路径
bool D3D12ProceduralTerrainPipeline::CreateRootSignature(std::string* errorMessage)
{
    // t0 仅供像素着色器采样地形纹理，t1 和 t2 仅供程序化顶点着色器读取
    // 每个 table 独占一个 range，后续新增 UAV 时不会改变现有根参数编号
    std::array<D3D12_DESCRIPTOR_RANGE, 3> srvRanges{};
    for (std::size_t index = 0; index < srvRanges.size(); ++index)
    {
        srvRanges[index].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV; // t0 到 t2 均为只读资源
        srvRanges[index].NumDescriptors = 1; // 每个根表只暴露一个稳定槽位
        srvRanges[index].BaseShaderRegister = static_cast<UINT>(index); // 数组索引直接映射 shader register
        srvRanges[index].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND; // 表内从零开始
    }

    // range 指针只在当前函数序列化期间使用，局部数组生命周期覆盖调用
    std::array<D3D12_ROOT_PARAMETER, 4> parameters{};
    parameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV; // b0 直接使用 GPU 虚拟地址
    parameters[0].Descriptor.ShaderRegister = 0; // TerrainConstants 固定寄存器
    parameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL; // VS 和 PS 共用帧常量
    for (std::size_t index = 0; index < srvRanges.size(); ++index)
    {
        D3D12_ROOT_PARAMETER& parameter = parameters[index + 1U];
        parameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE; // 引用后端共享可见堆
        parameter.DescriptorTable.NumDescriptorRanges = 1; // 每个参数保持单资源协议
        parameter.DescriptorTable.pDescriptorRanges = &srvRanges[index]; // 局部 range 在序列化前有效
        // 限制 shader visibility 可缩小驱动验证范围并暴露错误阶段绑定
        parameter.ShaderVisibility = index == 0U
            ? D3D12_SHADER_VISIBILITY_PIXEL
            : D3D12_SHADER_VISIBILITY_VERTEX;
    }

    // 地形纹理沿用线性 wrap sampler，确保像素着色器与普通 terrain 表现一致
    D3D12_STATIC_SAMPLER_DESC sampler{};
    sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR; // 与普通地形材质过滤一致
    sampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP; // 地表纹理横向重复
    sampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP; // 地表纹理纵向重复
    sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP; // 完整初始化静态 sampler
    sampler.ComparisonFunc = D3D12_COMPARISON_FUNC_ALWAYS; // 非比较采样器
    sampler.MaxLOD = D3D12_FLOAT32_MAX; // 允许完整 mip 范围
    sampler.ShaderRegister = 0; // 对应 TerrainPS 的 s0
    sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL; // 顶点阶段不采样材质

    D3D12_ROOT_SIGNATURE_DESC description{};
    description.NumParameters = static_cast<UINT>(parameters.size()); // b0 加三个 SRV 表
    description.pParameters = parameters.data(); // 序列化期间借用局部数组
    description.NumStaticSamplers = 1; // 仅地表纹理需要采样器
    description.pStaticSamplers = &sampler; // 静态 sampler 不占描述符堆
    // 当前路径不使用曲面细分和几何阶段，显式拒绝无关根访问
    description.Flags = D3D12_ROOT_SIGNATURE_FLAG_DENY_HULL_SHADER_ROOT_ACCESS |
                        D3D12_ROOT_SIGNATURE_FLAG_DENY_DOMAIN_SHADER_ROOT_ACCESS |
                        D3D12_ROOT_SIGNATURE_FLAG_DENY_GEOMETRY_SHADER_ROOT_ACCESS;

    Microsoft::WRL::ComPtr<ID3DBlob> serialized;
    Microsoft::WRL::ComPtr<ID3DBlob> errors;
    // 序列化错误 blob 通常包含寄存器冲突，是最有价值的初始化诊断信息
    HRESULT result = D3D12SerializeRootSignature(
        &description,
        D3D_ROOT_SIGNATURE_VERSION_1,
        &serialized,
        &errors);
    if (FAILED(result))
    {
        const char* detail = errors != nullptr ? static_cast<const char*>(errors->GetBufferPointer()) : "unknown error";
        SetError(errorMessage, std::string{"D3D12 procedural root signature serialization failed: "} + detail);
        return false;
    }
    result = _backend->Device()->CreateRootSignature(
        0,
        serialized->GetBufferPointer(),
        serialized->GetBufferSize(),
        IID_PPV_ARGS(&_rootSignature));
    if (FAILED(result))
    {
        SetError(errorMessage, "D3D12 procedural root signature creation failed");
        return false;
    }
    return true;
}

// 两个 PSO 共用 shader 和 root signature，仅区分填充模式
// 创建过程不依赖算法 buffer，因此可在 renderer 初始化时一次完成
bool D3D12ProceduralTerrainPipeline::CreatePipelineStates(std::string* errorMessage)
{
#if defined(PARALLEL_ROAM_DX12_SHADER_DIR)
    // 构建目录优先使用刚编译的 shader，回退路径只服务手动资源布局
    const std::filesystem::path shaderDirectory{PARALLEL_ROAM_DX12_SHADER_DIR};
#else
    const std::filesystem::path shaderDirectory{"assets/shaders/dx12"};
#endif
    // 程序化 VS 与普通 terrain PS 共享输出语义，避免维护第二套材质实现
    const std::vector<std::uint8_t> vertexShader =
        ReadBinaryFile(shaderDirectory / "CbtProceduralTerrainVS.cso", errorMessage);
    if (vertexShader.empty())
    {
        return false;
    }
    const std::vector<std::uint8_t> pixelShader = ReadBinaryFile(shaderDirectory / "TerrainPS.cso", errorMessage);
    if (pixelShader.empty())
    {
        return false;
    }

    // 顶点数据完全由 SRV 和 SV_VertexID 提供，因此 PSO 不声明 IA 输入布局
    D3D12_GRAPHICS_PIPELINE_STATE_DESC description{};
    description.pRootSignature = _rootSignature.Get(); // 与 shader 寄存器布局配套
    description.VS = {vertexShader.data(), vertexShader.size()}; // SV_VertexID 程序化顶点入口
    description.PS = {pixelShader.data(), pixelShader.size()}; // 与普通地形共享材质入口
    description.BlendState = OpaqueBlendDescription(); // 地形完全覆盖颜色目标
    description.SampleMask = std::numeric_limits<UINT>::max(); // 单采样全部启用
    description.RasterizerState = RasterizerDescription(D3D12_FILL_MODE_SOLID); // 首个 PSO 为实心模式
    description.DepthStencilState = DepthStencilDescription(); // 与主深度缓冲约定一致
    description.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE; // DRAW 输出三角形列表
    description.NumRenderTargets = 1; // 仅交换链颜色目标
    description.RTVFormats[0] = _backend->RenderTargetFormat(); // 必须匹配当前 back buffer
    description.DSVFormat = _backend->DepthStencilFormat(); // 必须匹配共享深度资源
    description.SampleDesc.Count = 1; // 与交换链和深度目标一致
    HRESULT result = _backend->Device()->CreateGraphicsPipelineState(
        &description,
        IID_PPV_ARGS(&_fillPipelineState));
    if (FAILED(result))
    {
        SetError(errorMessage, "D3D12 procedural solid pipeline creation failed");
        return false;
    }

    // 线框 PSO 只改变 rasterizer，根签名和 shader 字节码保持完全相同
    description.RasterizerState = RasterizerDescription(D3D12_FILL_MODE_WIREFRAME);
    result = _backend->Device()->CreateGraphicsPipelineState(
        &description,
        IID_PPV_ARGS(&_wireframePipelineState));
    if (FAILED(result))
    {
        SetError(errorMessage, "D3D12 procedural wireframe pipeline creation failed");
        return false;
    }
    return true;
}

// 命令签名不含 root arguments，所有根绑定由 RecordDraw 直接设置
bool D3D12ProceduralTerrainPipeline::CreateCommandSignature(std::string* errorMessage)
{
    // 参数缓冲由算法写入 D3D12_DRAW_ARGUMENTS，ByteStride 必须精确匹配
    D3D12_INDIRECT_ARGUMENT_DESC argument{};
    argument.Type = D3D12_INDIRECT_ARGUMENT_TYPE_DRAW; // CBT 输出非索引绘制参数
    D3D12_COMMAND_SIGNATURE_DESC signature{};
    signature.ByteStride = sizeof(D3D12_DRAW_ARGUMENTS); // 与算法写入结构精确一致
    signature.NumArgumentDescs = 1; // 每条命令只执行一次 DRAW
    signature.pArgumentDescs = &argument; // 创建期间借用局部描述
    if (FAILED(_backend->Device()->CreateCommandSignature(
            &signature,
            nullptr,
            IID_PPV_ARGS(&_drawCommandSignature))))
    {
        SetError(errorMessage, "D3D12 procedural command signature creation failed");
        return false;
    }
    return true;
}

// 分配数量与交换链 FrameCount 固定对应，resize 不需要重新分配
bool D3D12ProceduralTerrainPipeline::AllocateFrameDescriptors(std::string* errorMessage)
{
    // 描述符内容可变且会被 GPU 异步读取，所以每个交换链帧持有独立槽位
    // 部分分配失败由 Initialize 的 Shutdown 统一归还，不在循环内重复清理
    for (std::uint32_t frameIndex = 0; frameIndex < D3D12GraphicsBackend::FrameCount; ++frameIndex)
    {
        _activeElementSrvs[frameIndex] = _backend->AllocateSrvDescriptor();
        _vertexSrvs[frameIndex] = _backend->AllocateSrvDescriptor();
        if (!_activeElementSrvs[frameIndex].IsValid() || !_vertexSrvs[frameIndex].IsValid())
        {
            SetError(errorMessage, "D3D12 SRV heap has no descriptors for procedural terrain rendering");
            return false;
        }
    }
    return true;
}
} // namespace ParallelRoam::Render
