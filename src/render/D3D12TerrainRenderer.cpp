#include "render/TerrainRenderer.h"

#include "algorithms/classic_roam/ClassicRoamTerrainLodAlgorithm.h"
#include "algorithms/data_oriented_roam/DataOrientedRoamTerrainLodAlgorithm.h"
#include "algorithms/gpu_roam/d3d12/D3D12GpuRoamTerrainLodAlgorithm.h"
#include "render/D3D12GraphicsBackend.h"

#include <stb_image.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iostream>
#include <limits>
#include <memory>
#include <utility>
#include <vector>

namespace ParallelRoam::Render
{
namespace
{
constexpr float MinRoamRebuildDistance = 0.30F;
constexpr float RoamRebuildTerrainScale = 0.01F;
constexpr std::size_t ConstantBufferSize = 256U;

struct D3D12MeshFrameResources
{
    Microsoft::WRL::ComPtr<ID3D12Resource> VertexBuffer;
    Microsoft::WRL::ComPtr<ID3D12Resource> IndexBuffer;
    std::uint8_t* MappedVertices{nullptr};
    std::uint8_t* MappedIndices{nullptr};
    std::size_t VertexCapacityBytes{0};
    std::size_t IndexCapacityBytes{0};
    std::uint64_t MeshGeneration{0};
    D3D12_VERTEX_BUFFER_VIEW VertexView{};
    D3D12_INDEX_BUFFER_VIEW IndexView{};
};

struct TerrainConstants
{
    glm::mat4 View{1.0F};
    glm::mat4 Projection{1.0F};
    glm::vec4 CameraPosition{0.0F, 0.0F, 0.0F, 1.0F};
    glm::vec4 LightDirection{0.0F, -1.0F, 0.0F, 0.0F};
    glm::vec4 LightColor{1.0F};
    glm::vec4 LightingParameters{0.0F};
    glm::ivec4 DebugParameters{0};
    std::array<std::uint32_t, 12> Padding{};
};

static_assert(sizeof(TerrainConstants) == ConstantBufferSize);

D3D12_HEAP_PROPERTIES HeapProperties(D3D12_HEAP_TYPE type)
{
    D3D12_HEAP_PROPERTIES properties{};
    properties.Type = type;
    properties.CreationNodeMask = 1;
    properties.VisibleNodeMask = 1;
    return properties;
}

D3D12_RESOURCE_DESC BufferDescription(std::uint64_t size)
{
    D3D12_RESOURCE_DESC description{};
    description.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    description.Width = std::max<std::uint64_t>(size, 1U);
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

bool NeedsMeshRebuild(const TerrainRenderSettings& previous, const TerrainRenderSettings& next)
{
    return previous.TerrainSize != next.TerrainSize ||
           previous.HeightScale != next.HeightScale ||
           previous.UseTerrainLod != next.UseTerrainLod ||
           previous.TerrainLodAlgorithm != next.TerrainLodAlgorithm ||
           previous.RoamMaxDepth != next.RoamMaxDepth ||
           previous.RoamSplitThreshold != next.RoamSplitThreshold ||
           previous.RoamMergeThreshold != next.RoamMergeThreshold ||
           previous.RoamDistanceScale != next.RoamDistanceScale ||
           previous.RoamEnableLocalConstraints != next.RoamEnableLocalConstraints ||
           previous.RoamEnableTopologyValidation != next.RoamEnableTopologyValidation;
}

glm::vec3 NormalizeLightDirection(const glm::vec3& lightDirection)
{
    if (glm::dot(lightDirection, lightDirection) <= 0.000001F)
    {
        return glm::normalize(glm::vec3{-0.45F, -1.0F, -0.35F});
    }
    return glm::normalize(lightDirection);
}

std::unique_ptr<Algorithms::ITerrainLodAlgorithm> CreateTerrainLodAlgorithm(
    Algorithms::TerrainLodAlgorithmId algorithmId,
    D3D12GraphicsBackend& backend)
{
    if (algorithmId == Algorithms::TerrainLodAlgorithmId::ClassicCpuRoam)
    {
        return std::make_unique<Algorithms::ClassicRoam::ClassicRoamTerrainLodAlgorithm>();
    }
    if (algorithmId == Algorithms::TerrainLodAlgorithmId::DataOrientedCpuRoam)
    {
        return std::make_unique<Algorithms::DataOrientedRoam::DataOrientedRoamTerrainLodAlgorithm>();
    }
    if (algorithmId == Algorithms::TerrainLodAlgorithmId::GpuRoamLike)
    {
        return std::make_unique<Algorithms::GpuRoam::D3D12::D3D12GpuRoamTerrainLodAlgorithm>(backend);
    }
    return nullptr;
}

std::vector<std::uint8_t> ReadBinaryFile(const std::filesystem::path& path, std::string* errorMessage)
{
    std::ifstream stream{path, std::ios::binary | std::ios::ate};
    if (!stream)
    {
        SetError(errorMessage, "Failed to open compiled D3D12 shader: " + path.string());
        return {};
    }
    const std::streamsize size = stream.tellg();
    if (size <= 0)
    {
        SetError(errorMessage, "Compiled D3D12 shader is empty: " + path.string());
        return {};
    }
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
    stream.seekg(0, std::ios::beg);
    if (!stream.read(reinterpret_cast<char*>(bytes.data()), size))
    {
        SetError(errorMessage, "Failed to read compiled D3D12 shader: " + path.string());
        return {};
    }
    return bytes;
}

bool CreateMappedUploadBuffer(
    ID3D12Device* device,
    std::size_t size,
    Microsoft::WRL::ComPtr<ID3D12Resource>& resource,
    std::uint8_t*& mappedMemory,
    std::string* errorMessage)
{
    const D3D12_HEAP_PROPERTIES heapProperties = HeapProperties(D3D12_HEAP_TYPE_UPLOAD);
    const D3D12_RESOURCE_DESC description = BufferDescription(size);
    const HRESULT result = device->CreateCommittedResource(
        &heapProperties,
        D3D12_HEAP_FLAG_NONE,
        &description,
        D3D12_RESOURCE_STATE_GENERIC_READ,
        nullptr,
        IID_PPV_ARGS(&resource));
    if (FAILED(result))
    {
        SetError(errorMessage, "D3D12 upload buffer allocation failed");
        return false;
    }
    const D3D12_RANGE noReadRange{0, 0};
    void* mapped = nullptr;
    if (FAILED(resource->Map(0, &noReadRange, &mapped)))
    {
        SetError(errorMessage, "D3D12 upload buffer mapping failed");
        resource.Reset();
        return false;
    }
    mappedMemory = static_cast<std::uint8_t*>(mapped);
    return true;
}

D3D12_BLEND_DESC OpaqueBlendDescription()
{
    D3D12_BLEND_DESC description{};
    description.RenderTarget[0].BlendEnable = FALSE;
    description.RenderTarget[0].LogicOpEnable = FALSE;
    description.RenderTarget[0].SrcBlend = D3D12_BLEND_ONE;
    description.RenderTarget[0].DestBlend = D3D12_BLEND_ZERO;
    description.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
    description.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ONE;
    description.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_ZERO;
    description.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;
    description.RenderTarget[0].LogicOp = D3D12_LOGIC_OP_NOOP;
    description.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    return description;
}

D3D12_RASTERIZER_DESC RasterizerDescription(D3D12_FILL_MODE fillMode)
{
    D3D12_RASTERIZER_DESC description{};
    description.FillMode = fillMode;
    description.CullMode = D3D12_CULL_MODE_NONE;
    description.DepthClipEnable = TRUE;
    return description;
}

D3D12_DEPTH_STENCIL_DESC DepthStencilDescription()
{
    D3D12_DEPTH_STENCIL_DESC description{};
    description.DepthEnable = TRUE;
    description.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
    description.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
    description.StencilEnable = FALSE;
    return description;
}
} // namespace

struct D3D12TerrainRendererState
{
    D3D12GraphicsBackend* Backend{nullptr};
    Microsoft::WRL::ComPtr<ID3D12RootSignature> RootSignature;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> FillPipelineState;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> WireframePipelineState;
    Microsoft::WRL::ComPtr<ID3D12CommandSignature> DrawIndexedCommandSignature;
    std::array<D3D12MeshFrameResources, D3D12GraphicsBackend::FrameCount> MeshFrames;
    std::array<Microsoft::WRL::ComPtr<ID3D12Resource>, D3D12GraphicsBackend::FrameCount> ConstantBuffers;
    std::array<std::uint8_t*, D3D12GraphicsBackend::FrameCount> MappedConstants{};
    Microsoft::WRL::ComPtr<ID3D12Resource> Texture;
    D3D12DescriptorAllocation TextureSrv;
    ID3D12Resource* GpuVertexBuffer{nullptr};
    ID3D12Resource* GpuIndexBuffer{nullptr};
    ID3D12Resource* GpuIndirectBuffer{nullptr};
    D3D12_VERTEX_BUFFER_VIEW GpuVertexView{};
    D3D12_INDEX_BUFFER_VIEW GpuIndexView{};
    std::uint64_t MeshGeneration{0};
};

namespace
{
bool CreateRootSignature(D3D12TerrainRendererState& state, std::string* errorMessage)
{
    D3D12_DESCRIPTOR_RANGE srvRange{};
    srvRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    srvRange.NumDescriptors = 1;
    srvRange.BaseShaderRegister = 0;
    srvRange.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    std::array<D3D12_ROOT_PARAMETER, 2> parameters{};
    parameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    parameters[0].Descriptor.ShaderRegister = 0;
    parameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    parameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    parameters[1].DescriptorTable.NumDescriptorRanges = 1;
    parameters[1].DescriptorTable.pDescriptorRanges = &srvRange;
    parameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    D3D12_STATIC_SAMPLER_DESC sampler{};
    sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    sampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    sampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    sampler.ComparisonFunc = D3D12_COMPARISON_FUNC_ALWAYS;
    sampler.MaxLOD = D3D12_FLOAT32_MAX;
    sampler.ShaderRegister = 0;
    sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    D3D12_ROOT_SIGNATURE_DESC description{};
    description.NumParameters = static_cast<UINT>(parameters.size());
    description.pParameters = parameters.data();
    description.NumStaticSamplers = 1;
    description.pStaticSamplers = &sampler;
    description.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT |
                        D3D12_ROOT_SIGNATURE_FLAG_DENY_HULL_SHADER_ROOT_ACCESS |
                        D3D12_ROOT_SIGNATURE_FLAG_DENY_DOMAIN_SHADER_ROOT_ACCESS |
                        D3D12_ROOT_SIGNATURE_FLAG_DENY_GEOMETRY_SHADER_ROOT_ACCESS;

    Microsoft::WRL::ComPtr<ID3DBlob> serialized;
    Microsoft::WRL::ComPtr<ID3DBlob> errors;
    HRESULT result = D3D12SerializeRootSignature(
        &description,
        D3D_ROOT_SIGNATURE_VERSION_1,
        &serialized,
        &errors);
    if (FAILED(result))
    {
        const char* detail = errors != nullptr ? static_cast<const char*>(errors->GetBufferPointer()) : "unknown error";
        SetError(errorMessage, std::string{"D3D12 root signature serialization failed: "} + detail);
        return false;
    }
    result = state.Backend->Device()->CreateRootSignature(
        0,
        serialized->GetBufferPointer(),
        serialized->GetBufferSize(),
        IID_PPV_ARGS(&state.RootSignature));
    if (FAILED(result))
    {
        SetError(errorMessage, "D3D12 root signature creation failed");
        return false;
    }
    return true;
}

bool CreatePipelineStates(D3D12TerrainRendererState& state, std::string* errorMessage)
{
#if defined(PARALLEL_ROAM_DX12_SHADER_DIR)
    const std::filesystem::path shaderDirectory{PARALLEL_ROAM_DX12_SHADER_DIR};
#else
    const std::filesystem::path shaderDirectory{"assets/shaders/dx12"};
#endif
    const std::vector<std::uint8_t> vertexShader = ReadBinaryFile(shaderDirectory / "TerrainVS.cso", errorMessage);
    if (vertexShader.empty())
    {
        return false;
    }
    const std::vector<std::uint8_t> pixelShader = ReadBinaryFile(shaderDirectory / "TerrainPS.cso", errorMessage);
    if (pixelShader.empty())
    {
        return false;
    }

    const std::array<D3D12_INPUT_ELEMENT_DESC, 6> inputElements{{
        {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, static_cast<UINT>(offsetof(Terrain::TerrainMeshVertex, Position)), D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, static_cast<UINT>(offsetof(Terrain::TerrainMeshVertex, Normal)), D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, static_cast<UINT>(offsetof(Terrain::TerrainMeshVertex, TexCoord)), D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"TEXCOORD", 1, DXGI_FORMAT_R32_FLOAT, 0, static_cast<UINT>(offsetof(Terrain::TerrainMeshVertex, Height)), D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"COLOR", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, static_cast<UINT>(offsetof(Terrain::TerrainMeshVertex, DebugColor)), D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"TEXCOORD", 2, DXGI_FORMAT_R32_FLOAT, 0, static_cast<UINT>(offsetof(Terrain::TerrainMeshVertex, DebugHighlight)), D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
    }};

    D3D12_GRAPHICS_PIPELINE_STATE_DESC description{};
    description.pRootSignature = state.RootSignature.Get();
    description.VS = {vertexShader.data(), vertexShader.size()};
    description.PS = {pixelShader.data(), pixelShader.size()};
    description.BlendState = OpaqueBlendDescription();
    description.SampleMask = std::numeric_limits<UINT>::max();
    description.RasterizerState = RasterizerDescription(D3D12_FILL_MODE_SOLID);
    description.DepthStencilState = DepthStencilDescription();
    description.InputLayout = {inputElements.data(), static_cast<UINT>(inputElements.size())};
    description.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    description.NumRenderTargets = 1;
    description.RTVFormats[0] = state.Backend->RenderTargetFormat();
    description.DSVFormat = state.Backend->DepthStencilFormat();
    description.SampleDesc.Count = 1;
    HRESULT result = state.Backend->Device()->CreateGraphicsPipelineState(
        &description,
        IID_PPV_ARGS(&state.FillPipelineState));
    if (FAILED(result))
    {
        SetError(errorMessage, "D3D12 solid terrain pipeline creation failed");
        return false;
    }
    description.RasterizerState = RasterizerDescription(D3D12_FILL_MODE_WIREFRAME);
    result = state.Backend->Device()->CreateGraphicsPipelineState(
        &description,
        IID_PPV_ARGS(&state.WireframePipelineState));
    if (FAILED(result))
    {
        SetError(errorMessage, "D3D12 wireframe terrain pipeline creation failed");
        return false;
    }
    return true;
}

bool InitializeState(D3D12TerrainRendererState& state, std::string* errorMessage)
{
    if (!CreateRootSignature(state, errorMessage) || !CreatePipelineStates(state, errorMessage))
    {
        return false;
    }
    for (std::uint32_t frameIndex = 0; frameIndex < D3D12GraphicsBackend::FrameCount; ++frameIndex)
    {
        if (!CreateMappedUploadBuffer(
                state.Backend->Device(),
                ConstantBufferSize,
                state.ConstantBuffers[frameIndex],
                state.MappedConstants[frameIndex],
                errorMessage))
        {
            return false;
        }
    }

    D3D12_INDIRECT_ARGUMENT_DESC argument{};
    argument.Type = D3D12_INDIRECT_ARGUMENT_TYPE_DRAW_INDEXED;
    D3D12_COMMAND_SIGNATURE_DESC signature{};
    signature.ByteStride = sizeof(D3D12_DRAW_INDEXED_ARGUMENTS);
    signature.NumArgumentDescs = 1;
    signature.pArgumentDescs = &argument;
    if (FAILED(state.Backend->Device()->CreateCommandSignature(
            &signature,
            nullptr,
            IID_PPV_ARGS(&state.DrawIndexedCommandSignature))))
    {
        SetError(errorMessage, "D3D12 terrain indirect command signature creation failed");
        return false;
    }
    return true;
}

void ReleaseMappedResource(Microsoft::WRL::ComPtr<ID3D12Resource>& resource, std::uint8_t*& mapped)
{
    if (resource != nullptr && mapped != nullptr)
    {
        resource->Unmap(0, nullptr);
    }
    mapped = nullptr;
    resource.Reset();
}

bool UploadMeshForFrame(
    D3D12TerrainRendererState& state,
    const Terrain::TerrainMeshData& meshData,
    std::uint32_t frameIndex,
    std::string* errorMessage)
{
    D3D12MeshFrameResources& frame = state.MeshFrames[frameIndex];
    if (frame.MeshGeneration == state.MeshGeneration)
    {
        return true;
    }

    const std::size_t vertexBytes = meshData.Vertices.size() * sizeof(Terrain::TerrainMeshVertex);
    const std::size_t indexBytes = meshData.Indices.size() * sizeof(std::uint32_t);
    if (vertexBytes == 0 || indexBytes == 0)
    {
        SetError(errorMessage, "D3D12 terrain mesh upload received empty data");
        return false;
    }

    if (frame.VertexCapacityBytes < vertexBytes)
    {
        ReleaseMappedResource(frame.VertexBuffer, frame.MappedVertices);
        if (!CreateMappedUploadBuffer(
                state.Backend->Device(), vertexBytes, frame.VertexBuffer, frame.MappedVertices, errorMessage))
        {
            return false;
        }
        frame.VertexCapacityBytes = vertexBytes;
    }
    if (frame.IndexCapacityBytes < indexBytes)
    {
        ReleaseMappedResource(frame.IndexBuffer, frame.MappedIndices);
        if (!CreateMappedUploadBuffer(
                state.Backend->Device(), indexBytes, frame.IndexBuffer, frame.MappedIndices, errorMessage))
        {
            return false;
        }
        frame.IndexCapacityBytes = indexBytes;
    }

    std::memcpy(frame.MappedVertices, meshData.Vertices.data(), vertexBytes);
    std::memcpy(frame.MappedIndices, meshData.Indices.data(), indexBytes);
    frame.VertexView.BufferLocation = frame.VertexBuffer->GetGPUVirtualAddress();
    frame.VertexView.SizeInBytes = static_cast<UINT>(vertexBytes);
    frame.VertexView.StrideInBytes = sizeof(Terrain::TerrainMeshVertex);
    frame.IndexView.BufferLocation = frame.IndexBuffer->GetGPUVirtualAddress();
    frame.IndexView.SizeInBytes = static_cast<UINT>(indexBytes);
    frame.IndexView.Format = DXGI_FORMAT_R32_UINT;
    frame.MeshGeneration = state.MeshGeneration;
    return true;
}
} // namespace

TerrainRenderer::TerrainRenderer() = default;

TerrainRenderer::~TerrainRenderer()
{
    Shutdown();
}

bool TerrainRenderer::Initialize(
    IGraphicsBackend& graphicsBackend,
    const std::filesystem::path& heightMapPath,
    const std::filesystem::path& texturePath,
    const TerrainRenderSettings& settings,
    std::string* errorMessage)
{
    if (graphicsBackend.Api() != GraphicsApi::Direct3D12)
    {
        SetError(errorMessage, "D3D12 terrain renderer received a non-D3D12 graphics backend");
        return false;
    }
    auto* backend = dynamic_cast<D3D12GraphicsBackend*>(&graphicsBackend);
    if (backend == nullptr)
    {
        SetError(errorMessage, "D3D12 terrain renderer could not access the concrete backend");
        return false;
    }

    _graphicsBackend = &graphicsBackend;
    _d3d12State = std::make_unique<D3D12TerrainRendererState>();
    _d3d12State->Backend = backend;
    _settings = settings;
    _heightMapPath = heightMapPath;
    _texturePath = texturePath;
    if (!InitializeState(*_d3d12State, errorMessage) ||
        !_heightMap.LoadFromFile(heightMapPath, errorMessage))
    {
        Shutdown();
        return false;
    }
    if (_settings.TerrainLodAlgorithm == Algorithms::TerrainLodAlgorithmId::GpuRoamLike)
    {
        // DX12 compute commands require an open frame command list; the first UpdateForCamera performs the build.
        _meshDirty = true;
    }
    else if (!RebuildMesh(errorMessage))
    {
        Shutdown();
        return false;
    }
    if (!LoadTexture(texturePath, errorMessage))
    {
        Shutdown();
        return false;
    }
    _initialized = true;
    return true;
}

bool TerrainRenderer::ApplySettings(const TerrainRenderSettings& settings, std::string* errorMessage)
{
    const bool rebuildMesh = NeedsMeshRebuild(_settings, settings);
    _settings = settings;
    _meshDirty = _meshDirty || rebuildMesh;
    if (_meshDirty &&
        _settings.UseTerrainLod &&
        _settings.TerrainLodAlgorithm == Algorithms::TerrainLodAlgorithmId::GpuRoamLike &&
        (_d3d12State == nullptr || !_d3d12State->Backend->FrameOpen()))
    {
        return true;
    }
    return !_meshDirty || RebuildMesh(errorMessage);
}

bool TerrainRenderer::LoadHeightMap(const std::filesystem::path& heightMapPath, std::string* errorMessage)
{
    Terrain::HeightMap nextHeightMap;
    if (!nextHeightMap.LoadFromFile(heightMapPath, errorMessage))
    {
        return false;
    }
    _heightMap = std::move(nextHeightMap);
    _heightMapPath = heightMapPath;
    ResetTerrainLodAlgorithm();
    if (_settings.UseTerrainLod &&
        _settings.TerrainLodAlgorithm == Algorithms::TerrainLodAlgorithmId::GpuRoamLike &&
        (_d3d12State == nullptr || !_d3d12State->Backend->FrameOpen()))
    {
        return true;
    }
    return RebuildMesh(errorMessage);
}

bool TerrainRenderer::UpdateForCamera(const glm::vec3& cameraPosition, std::string* errorMessage)
{
    _lastCameraPosition = cameraPosition;
    if (!_settings.UseTerrainLod && !_meshDirty)
    {
        return true;
    }
    if (_settings.UseTerrainLod)
    {
        const float rebuildDistance = std::max(
            _settings.TerrainSize * RoamRebuildTerrainScale,
            MinRoamRebuildDistance);
        const glm::vec3 buildDelta = cameraPosition - _lastRoamBuildCameraPosition;
        const bool cameraMovedEnough = !_hasRoamBuildCameraPosition ||
            glm::dot(buildDelta, buildDelta) >= rebuildDistance * rebuildDistance;
        if (!_meshDirty && !cameraMovedEnough)
        {
            return true;
        }
        return RebuildTerrainLod(cameraPosition, errorMessage);
    }
    return RebuildMesh(errorMessage);
}

void TerrainRenderer::RequestMeshRebuild()
{
    _meshDirty = true;
}

void TerrainRenderer::ResetTerrainLodAlgorithm()
{
    if (_terrainLodAlgorithm != nullptr &&
        _terrainLodAlgorithm->Info().Id == Algorithms::TerrainLodAlgorithmId::GpuRoamLike &&
        _d3d12State != nullptr)
    {
        _d3d12State->Backend->WaitForGpuIdle();
    }
    _terrainLodAlgorithm.reset();
    _terrainLodStats = {};
    _terrainLodStatusMessage.clear();
    _terrainLodTotalMilliseconds = 0.0F;
    _terrainLodCpuUploadMilliseconds = 0.0F;
    _drawVertexCount = 0U;
    _drawIndexCount = 0U;
    _drawTriangleCount = 0U;
    _renderMode = Algorithms::TerrainLodRenderMode::CpuMesh;
    if (_d3d12State != nullptr)
    {
        _d3d12State->GpuVertexBuffer = nullptr;
        _d3d12State->GpuIndexBuffer = nullptr;
        _d3d12State->GpuIndirectBuffer = nullptr;
        _d3d12State->GpuVertexView = {};
        _d3d12State->GpuIndexView = {};
    }
    _hasRoamBuildCameraPosition = false;
    _meshDirty = true;
}

void TerrainRenderer::Shutdown()
{
    _terrainLodAlgorithm.reset();
    _terrainLodStats = {};
    _terrainLodStatusMessage.clear();
    if (_d3d12State != nullptr)
    {
        for (D3D12MeshFrameResources& frame : _d3d12State->MeshFrames)
        {
            ReleaseMappedResource(frame.VertexBuffer, frame.MappedVertices);
            ReleaseMappedResource(frame.IndexBuffer, frame.MappedIndices);
        }
        for (std::uint32_t index = 0; index < D3D12GraphicsBackend::FrameCount; ++index)
        {
            ReleaseMappedResource(_d3d12State->ConstantBuffers[index], _d3d12State->MappedConstants[index]);
        }
        if (_d3d12State->Backend != nullptr)
        {
            _d3d12State->Backend->ReleaseSrvDescriptor(_d3d12State->TextureSrv);
        }
        _d3d12State.reset();
    }
    _drawVertexCount = 0U;
    _drawIndexCount = 0U;
    _drawTriangleCount = 0U;
    _renderMode = Algorithms::TerrainLodRenderMode::CpuMesh;
    _graphicsBackend = nullptr;
    _initialized = false;
}

void TerrainRenderer::Render(const RenderContext& context)
{
    if (!_initialized || !HasDrawableTerrain() || _d3d12State == nullptr)
    {
        return;
    }
    D3D12GraphicsBackend& backend = *_d3d12State->Backend;
    ID3D12GraphicsCommandList* commandList = backend.CommandList();
    if (commandList == nullptr)
    {
        return;
    }

    const std::uint32_t frameIndex = backend.CurrentFrameIndex();
    if (_renderMode == Algorithms::TerrainLodRenderMode::CpuMesh)
    {
        std::string uploadError;
        if (!UploadMeshForFrame(*_d3d12State, _meshData, frameIndex, &uploadError))
        {
            std::cerr << uploadError << '\n';
            return;
        }
    }

    TerrainConstants constants{};
    constants.View = context.View;
    constants.Projection = context.Projection;
    constants.CameraPosition = glm::vec4{context.CameraPosition, 1.0F};
    constants.LightDirection = glm::vec4{NormalizeLightDirection(_settings.LightDirection), 0.0F};
    constants.LightColor = glm::vec4{_settings.LightColor, 1.0F};
    constants.LightingParameters = glm::vec4{
        _settings.AmbientStrength,
        _settings.DiffuseStrength,
        _settings.SpecularStrength,
        _settings.DebugOverlayStrength};
    constants.DebugParameters.x = static_cast<int>(_settings.DebugColorMode);
    std::memcpy(_d3d12State->MappedConstants[frameIndex], &constants, sizeof(constants));

    ID3D12DescriptorHeap* graphicsHeaps[] = {backend.ShaderVisibleSrvHeap()};
    commandList->SetDescriptorHeaps(1, graphicsHeaps);
    commandList->SetPipelineState(
        _settings.Wireframe ? _d3d12State->WireframePipelineState.Get() : _d3d12State->FillPipelineState.Get());
    commandList->SetGraphicsRootSignature(_d3d12State->RootSignature.Get());
    commandList->SetGraphicsRootConstantBufferView(
        0,
        _d3d12State->ConstantBuffers[frameIndex]->GetGPUVirtualAddress());
    commandList->SetGraphicsRootDescriptorTable(1, _d3d12State->TextureSrv.Gpu);
    commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    if (_renderMode == Algorithms::TerrainLodRenderMode::GpuIndirect)
    {
        commandList->IASetVertexBuffers(0, 1, &_d3d12State->GpuVertexView);
        commandList->IASetIndexBuffer(&_d3d12State->GpuIndexView);
        commandList->ExecuteIndirect(
            _d3d12State->DrawIndexedCommandSignature.Get(),
            1,
            _d3d12State->GpuIndirectBuffer,
            0,
            nullptr,
            0);
    }
    else
    {
        D3D12MeshFrameResources& frame = _d3d12State->MeshFrames[frameIndex];
        commandList->IASetVertexBuffers(0, 1, &frame.VertexView);
        commandList->IASetIndexBuffer(&frame.IndexView);
        commandList->DrawIndexedInstanced(static_cast<UINT>(_drawIndexCount), 1, 0, 0, 0);
    }
}

TerrainRenderStats TerrainRenderer::Stats() const
{
    TerrainRenderStats stats{};
    stats.HeightMapPath = _heightMapPath;
    stats.HeightMapWidth = _heightMap.Width();
    stats.HeightMapHeight = _heightMap.Height();
    stats.VertexCount = _drawVertexCount;
    stats.TriangleCount = _drawTriangleCount;
    stats.DrawCallCount = _initialized && HasDrawableTerrain() ? 1 : 0;
    stats.TerrainSize = _settings.TerrainSize;
    stats.HeightScale = _settings.HeightScale;
    stats.UseTerrainLod = _settings.UseTerrainLod;
    stats.TerrainLodAlgorithm = _settings.TerrainLodAlgorithm;
    stats.TerrainLodStatusMessage = _terrainLodStatusMessage;
    stats.RoamMaxDepthSetting = _settings.RoamMaxDepth;
    stats.RoamSplitThreshold = _settings.RoamSplitThreshold;
    stats.RoamMergeThreshold = _settings.RoamMergeThreshold;
    stats.RoamDistanceScale = _settings.RoamDistanceScale;
    stats.RoamNodeCount = _terrainLodStats.ActiveNodeCount;
    stats.RoamOriginalTriangleCount = _terrainLodStats.OriginalTriangleCount;
    stats.RoamSubdividedTriangleCount = _terrainLodStats.SubdividedTriangleCount;
    stats.RoamRebuiltTriangleCount = _terrainLodStats.RebuiltTriangleCount;
    stats.RoamActiveSplitCount = _terrainLodStats.ActiveSplitCount;
    stats.RoamSplitCount = _terrainLodStats.SplitCount;
    stats.RoamForcedSplitCount = _terrainLodStats.ForcedSplitCount;
    stats.RoamMergeCount = _terrainLodStats.MergeCount;
    stats.RoamCrackRiskCount = _terrainLodStats.CrackRiskCount;
    stats.RoamConstraintPassCount = _terrainLodStats.ConstraintPassCount;
    stats.RoamCandidatePeakCount = _terrainLodStats.CandidatePeakCount;
    stats.RoamRejectedSplitCount = _terrainLodStats.RejectedSplitCount;
    stats.RoamRejectedMergeCount = _terrainLodStats.RejectedMergeCount;
    stats.RoamTjunctionCount = _terrainLodStats.TjunctionCount;
    stats.RoamInvalidNeighborCount = _terrainLodStats.InvalidNeighborCount;
    stats.RoamInvalidTopologyCount = _terrainLodStats.InvalidTopologyCount;
    stats.RoamCpuWorkerCount = _terrainLodStats.CpuWorkerCount;
    stats.RoamCpuUtilizationPercent = _terrainLodStats.CpuUtilizationPercent;
    stats.RoamTotalMilliseconds = _terrainLodTotalMilliseconds;
    stats.RoamUpdateMilliseconds = _terrainLodStats.CpuUpdateMilliseconds;
    stats.RoamCpuUploadMilliseconds = _terrainLodCpuUploadMilliseconds;
    stats.RoamSplitMilliseconds = _terrainLodStats.SplitMilliseconds;
    stats.RoamMergeMilliseconds = _terrainLodStats.MergeMilliseconds;
    stats.RoamEmitMilliseconds = _terrainLodStats.EmitMilliseconds;
    stats.RoamValidateMilliseconds = _terrainLodStats.ValidateMilliseconds;
    stats.RoamGpuComputeMilliseconds = _terrainLodStats.GpuComputeMilliseconds;
    stats.RoamGpuSnapshotBuildMilliseconds = _terrainLodStats.GpuSnapshotBuildMilliseconds;
    stats.RoamGpuBufferAllocationMilliseconds = _terrainLodStats.GpuBufferAllocationMilliseconds;
    stats.RoamGpuDispatchWallMilliseconds = _terrainLodStats.GpuDispatchWallMilliseconds;
    stats.RoamGpuQueryWaitMilliseconds = _terrainLodStats.GpuQueryWaitMilliseconds;
    stats.RoamGpuReadbackWaitMilliseconds = _terrainLodStats.GpuReadbackWaitMilliseconds;
    stats.RoamFrameFenceWaitMilliseconds =
        _d3d12State != nullptr ? _d3d12State->Backend->LastGpuWaitMilliseconds() : 0.0F;
    stats.RoamRenderMilliseconds = _d3d12State != nullptr ? _d3d12State->Backend->LastGpuFrameMilliseconds() : 0.0F;
    stats.RoamCpuGpuUploadBytes = _terrainLodStats.CpuGpuUploadBytes;
    stats.RoamCpuGpuReadbackBytes = _terrainLodStats.CpuGpuReadbackBytes;
    stats.RoamMaxDepthReached = _terrainLodStats.MaxActiveDepth;
    return stats;
}

const std::filesystem::path& TerrainRenderer::HeightMapPath() const
{
    return _heightMapPath;
}

const std::filesystem::path& TerrainRenderer::TexturePath() const
{
    return _texturePath;
}

bool TerrainRenderer::RebuildMesh(std::string* errorMessage)
{
    return _settings.UseTerrainLod
        ? RebuildTerrainLod(_lastCameraPosition, errorMessage)
        : RebuildRegularGrid(errorMessage);
}

bool TerrainRenderer::RebuildRegularGrid(std::string* errorMessage)
{
    _meshData = Terrain::TerrainMeshBuilder::Build(_heightMap, _settings.TerrainSize, _settings.HeightScale);
    _terrainLodStats = {};
    _terrainLodStatusMessage.clear();
    _terrainLodTotalMilliseconds = 0.0F;
    _terrainLodCpuUploadMilliseconds = 0.0F;
    _terrainLodAlgorithm.reset();
    _hasRoamBuildCameraPosition = false;
    if (_meshData.Vertices.empty() || _meshData.Indices.empty())
    {
        SetError(errorMessage, "Terrain mesh build failed: invalid height map or grid size");
        return false;
    }
    _meshDirty = false;
    return UploadMesh(errorMessage);
}

bool TerrainRenderer::RebuildTerrainLod(const glm::vec3& cameraPosition, std::string* errorMessage)
{
    const auto rebuildStart = std::chrono::steady_clock::now();
    _terrainLodTotalMilliseconds = 0.0F;
    _terrainLodCpuUploadMilliseconds = 0.0F;
    if (_terrainLodAlgorithm == nullptr || _terrainLodAlgorithm->Info().Id != _settings.TerrainLodAlgorithm)
    {
        if (_terrainLodAlgorithm != nullptr &&
            (_terrainLodAlgorithm->Info().Id == Algorithms::TerrainLodAlgorithmId::GpuRoamLike ||
             _settings.TerrainLodAlgorithm == Algorithms::TerrainLodAlgorithmId::GpuRoamLike))
        {
            _d3d12State->Backend->WaitForGpuIdle();
        }
        _terrainLodAlgorithm = CreateTerrainLodAlgorithm(
            _settings.TerrainLodAlgorithm,
            *_d3d12State->Backend);
        _hasRoamBuildCameraPosition = false;
    }
    if (_terrainLodAlgorithm == nullptr)
    {
        _terrainLodStatusMessage = "D3D12 stage 4 supports Classic CPU ROAM and Data-Oriented CPU ROAM";
        SetError(errorMessage, _terrainLodStatusMessage);
        return false;
    }

    Algorithms::TerrainLodSettings lodSettings{};
    lodSettings.TerrainSize = _settings.TerrainSize;
    lodSettings.HeightScale = _settings.HeightScale;
    lodSettings.MaxDepth = _settings.RoamMaxDepth;
    lodSettings.SplitThreshold = _settings.RoamSplitThreshold;
    lodSettings.MergeThreshold = _settings.RoamMergeThreshold;
    lodSettings.DistanceScale = _settings.RoamDistanceScale;
    lodSettings.EnableLocalConstraints = _settings.RoamEnableLocalConstraints;
    lodSettings.EnableTopologyValidation = _settings.RoamEnableTopologyValidation;
    Algorithms::TerrainLodBuildInput buildInput{};
    buildInput.HeightMap = &_heightMap;
    buildInput.CameraPosition = cameraPosition;
    buildInput.Settings = lodSettings;
    Algorithms::TerrainLodRenderPacket renderPacket{};
    std::string localError;
    std::string* buildError = errorMessage != nullptr ? errorMessage : &localError;
    buildError->clear();
    if (!_terrainLodAlgorithm->BuildRenderData(buildInput, renderPacket, buildError))
    {
        _terrainLodStatusMessage = buildError->empty() ? "Terrain LOD build failed" : *buildError;
        return false;
    }
    if (!renderPacket.HasConsistentResourceContract())
    {
        _terrainLodStatusMessage = "D3D12 terrain LOD returned an inconsistent render packet";
        SetError(errorMessage, _terrainLodStatusMessage);
        return false;
    }

    _terrainLodStats = _terrainLodAlgorithm->Stats();
    _terrainLodStatusMessage = renderPacket.StatusMessage;
    _lastRoamBuildCameraPosition = cameraPosition;
    _hasRoamBuildCameraPosition = true;
    if (renderPacket.Mode == Algorithms::TerrainLodRenderMode::GpuIndirect &&
        renderPacket.NativeResourceApi == Algorithms::TerrainLodNativeResourceApi::Direct3D12)
    {
        _meshData = {};
        _renderMode = Algorithms::TerrainLodRenderMode::GpuIndirect;
        _d3d12State->GpuVertexBuffer = reinterpret_cast<ID3D12Resource*>(renderPacket.NativeVertexBuffer);
        _d3d12State->GpuIndexBuffer = reinterpret_cast<ID3D12Resource*>(renderPacket.NativeIndexBuffer);
        _d3d12State->GpuIndirectBuffer = reinterpret_cast<ID3D12Resource*>(renderPacket.NativeIndirectDrawBuffer);
        _drawVertexCount = renderPacket.ActiveTriangleCount * 3U;
        _drawIndexCount = renderPacket.IndexCount;
        _drawTriangleCount = renderPacket.ActiveTriangleCount;
        _d3d12State->GpuVertexView.BufferLocation = _d3d12State->GpuVertexBuffer->GetGPUVirtualAddress();
        _d3d12State->GpuVertexView.SizeInBytes = static_cast<UINT>(renderPacket.GpuVertexBufferCapacityBytes);
        _d3d12State->GpuVertexView.StrideInBytes = sizeof(Terrain::TerrainMeshVertex);
        _d3d12State->GpuIndexView.BufferLocation = _d3d12State->GpuIndexBuffer->GetGPUVirtualAddress();
        _d3d12State->GpuIndexView.SizeInBytes = static_cast<UINT>(renderPacket.GpuIndexBufferCapacityBytes);
        _d3d12State->GpuIndexView.Format = DXGI_FORMAT_R32_UINT;
        _meshDirty = false;
        _terrainLodTotalMilliseconds =
            std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - rebuildStart).count();
        return true;
    }

    if (renderPacket.Mode != Algorithms::TerrainLodRenderMode::CpuMesh)
    {
        _terrainLodStatusMessage = "D3D12 terrain LOD returned an unsupported render mode";
        SetError(errorMessage, _terrainLodStatusMessage);
        return false;
    }

    _meshData = std::move(renderPacket.CpuMesh);
    if (_meshData.Vertices.empty() || _meshData.Indices.empty())
    {
        _terrainLodStatusMessage = "Terrain LOD mesh build failed";
        SetError(errorMessage, _terrainLodStatusMessage);
        return false;
    }

    const auto uploadStart = std::chrono::steady_clock::now();
    if (!UploadMesh(errorMessage))
    {
        _meshDirty = true;
        return false;
    }
    _terrainLodCpuUploadMilliseconds =
        std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - uploadStart).count();
    _meshDirty = false;
    _terrainLodTotalMilliseconds =
        std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - rebuildStart).count();
    return true;
}

bool TerrainRenderer::UploadMesh(std::string* errorMessage)
{
    if (_d3d12State == nullptr || _meshData.Vertices.empty() || _meshData.Indices.empty())
    {
        SetError(errorMessage, "D3D12 terrain mesh upload state is incomplete");
        return false;
    }
    ++_d3d12State->MeshGeneration;
    _renderMode = Algorithms::TerrainLodRenderMode::CpuMesh;
    _drawVertexCount = _meshData.Vertices.size();
    _drawIndexCount = _meshData.Indices.size();
    _drawTriangleCount = _meshData.Indices.size() / 3U;
    return UploadMeshForFrame(
        *_d3d12State,
        _meshData,
        _d3d12State->Backend->CurrentFrameIndex(),
        errorMessage);
}

bool TerrainRenderer::LoadTexture(const std::filesystem::path& texturePath, std::string* errorMessage)
{
    int width = 0;
    int height = 0;
    int channels = 0;
    unsigned char* pixels = stbi_load(texturePath.string().c_str(), &width, &height, &channels, 4);
    if (pixels == nullptr)
    {
        SetError(errorMessage, "Terrain texture load failed: " + texturePath.string());
        return false;
    }

    D3D12_RESOURCE_DESC textureDescription{};
    textureDescription.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    textureDescription.Width = static_cast<UINT64>(width);
    textureDescription.Height = static_cast<UINT>(height);
    textureDescription.DepthOrArraySize = 1;
    textureDescription.MipLevels = 1;
    textureDescription.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    textureDescription.SampleDesc.Count = 1;
    textureDescription.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    const D3D12_HEAP_PROPERTIES defaultHeap = HeapProperties(D3D12_HEAP_TYPE_DEFAULT);
    HRESULT result = _d3d12State->Backend->Device()->CreateCommittedResource(
        &defaultHeap,
        D3D12_HEAP_FLAG_NONE,
        &textureDescription,
        D3D12_RESOURCE_STATE_COPY_DEST,
        nullptr,
        IID_PPV_ARGS(&_d3d12State->Texture));
    if (FAILED(result))
    {
        stbi_image_free(pixels);
        SetError(errorMessage, "D3D12 terrain texture allocation failed");
        return false;
    }

    D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};
    UINT rowCount = 0;
    UINT64 rowSize = 0;
    UINT64 uploadSize = 0;
    _d3d12State->Backend->Device()->GetCopyableFootprints(
        &textureDescription, 0, 1, 0, &footprint, &rowCount, &rowSize, &uploadSize);
    Microsoft::WRL::ComPtr<ID3D12Resource> uploadBuffer;
    std::uint8_t* mappedUpload = nullptr;
    if (!CreateMappedUploadBuffer(
            _d3d12State->Backend->Device(),
            static_cast<std::size_t>(uploadSize),
            uploadBuffer,
            mappedUpload,
            errorMessage))
    {
        stbi_image_free(pixels);
        return false;
    }
    const std::size_t sourceRowBytes = static_cast<std::size_t>(width) * 4U;
    for (UINT row = 0; row < rowCount; ++row)
    {
        std::memcpy(
            mappedUpload + footprint.Offset + static_cast<std::size_t>(row) * footprint.Footprint.RowPitch,
            pixels + static_cast<std::size_t>(row) * sourceRowBytes,
            sourceRowBytes);
    }
    stbi_image_free(pixels);

    const bool uploaded = _d3d12State->Backend->ExecuteImmediate(
        [&](ID3D12GraphicsCommandList* commandList, std::string*) {
            D3D12_TEXTURE_COPY_LOCATION source{};
            source.pResource = uploadBuffer.Get();
            source.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
            source.PlacedFootprint = footprint;
            D3D12_TEXTURE_COPY_LOCATION destination{};
            destination.pResource = _d3d12State->Texture.Get();
            destination.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
            commandList->CopyTextureRegion(&destination, 0, 0, 0, &source, nullptr);
            D3D12_RESOURCE_BARRIER barrier{};
            barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            barrier.Transition.pResource = _d3d12State->Texture.Get();
            barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
            barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
            barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
            commandList->ResourceBarrier(1, &barrier);
            return true;
        },
        errorMessage);
    ReleaseMappedResource(uploadBuffer, mappedUpload);
    if (!uploaded)
    {
        return false;
    }

    _d3d12State->TextureSrv = _d3d12State->Backend->AllocateSrvDescriptor();
    if (!_d3d12State->TextureSrv.IsValid())
    {
        SetError(errorMessage, "D3D12 SRV heap has no descriptor available for the terrain texture");
        return false;
    }
    D3D12_SHADER_RESOURCE_VIEW_DESC srvDescription{};
    srvDescription.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDescription.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    srvDescription.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvDescription.Texture2D.MipLevels = 1;
    _d3d12State->Backend->Device()->CreateShaderResourceView(
        _d3d12State->Texture.Get(),
        &srvDescription,
        _d3d12State->TextureSrv.Cpu);
    return true;
}

bool TerrainRenderer::HasDrawableTerrain() const
{
    if (_d3d12State == nullptr || _drawIndexCount == 0U)
    {
        return false;
    }
    if (_renderMode == Algorithms::TerrainLodRenderMode::GpuIndirect)
    {
        return _d3d12State->GpuVertexBuffer != nullptr &&
               _d3d12State->GpuIndexBuffer != nullptr &&
               _d3d12State->GpuIndirectBuffer != nullptr;
    }
    return _renderMode == Algorithms::TerrainLodRenderMode::CpuMesh;
}
} // namespace ParallelRoam::Render
