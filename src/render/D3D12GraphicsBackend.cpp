#include "render/D3D12GraphicsBackend.h"

#include "gui/ImGuiLayer.h"

#include <SDL_syswm.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <iostream>
#include <sstream>

namespace ParallelRoam::Render
{
namespace
{
constexpr DXGI_FORMAT BackBufferFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
constexpr DXGI_FORMAT DepthBufferFormat = DXGI_FORMAT_D32_FLOAT;
constexpr std::uint32_t SrvDescriptorCount = 256;

D3D12_HEAP_PROPERTIES HeapProperties(D3D12_HEAP_TYPE type)
{
    D3D12_HEAP_PROPERTIES properties{};
    properties.Type = type;
    properties.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
    properties.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
    properties.CreationNodeMask = 1;
    properties.VisibleNodeMask = 1;
    return properties;
}

std::string WideToUtf8(const wchar_t* text)
{
    if (text == nullptr || text[0] == L'\0')
    {
        return "Unknown D3D12 adapter";
    }

    const int requiredBytes = WideCharToMultiByte(CP_UTF8, 0, text, -1, nullptr, 0, nullptr, nullptr);
    if (requiredBytes <= 1)
    {
        return "Unknown D3D12 adapter";
    }

    std::string result(static_cast<std::size_t>(requiredBytes), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text, -1, result.data(), requiredBytes, nullptr, nullptr);
    result.pop_back();
    return result;
}

std::string HResultText(HRESULT result)
{
    char* messageBuffer = nullptr;
    const DWORD length = FormatMessageA(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr,
        static_cast<DWORD>(result),
        MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        reinterpret_cast<char*>(&messageBuffer),
        0,
        nullptr);

    std::ostringstream stream;
    stream << "HRESULT 0x" << std::hex << static_cast<unsigned long>(result);
    if (length > 0 && messageBuffer != nullptr)
    {
        stream << ": " << messageBuffer;
        LocalFree(messageBuffer);
    }
    return stream.str();
}

void SetError(std::string* errorMessage, const char* operation, HRESULT result)
{
    if (errorMessage != nullptr)
    {
        *errorMessage = std::string{operation} + " failed: " + HResultText(result);
    }
}
} // namespace

D3D12GraphicsBackend::~D3D12GraphicsBackend()
{
    Shutdown();
}

GraphicsApi D3D12GraphicsBackend::Api() const
{
    return GraphicsApi::Direct3D12;
}

const char* D3D12GraphicsBackend::Name() const
{
    return "D3D12";
}

bool D3D12GraphicsBackend::ConfigureWindow(std::string*)
{
    return true;
}

std::uint32_t D3D12GraphicsBackend::RequiredSdlWindowFlags() const
{
    return SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI;
}

bool D3D12GraphicsBackend::Initialize(SDL_Window* window, std::string* errorMessage)
{
    if (_initialized || window == nullptr)
    {
        if (errorMessage != nullptr)
        {
            *errorMessage = _initialized ? "D3D12 backend is already initialized" : "D3D12 requires a valid SDL window";
        }
        return false;
    }

    SDL_SysWMinfo windowInfo{};
    SDL_VERSION(&windowInfo.version);
    if (SDL_GetWindowWMInfo(window, &windowInfo) != SDL_TRUE || windowInfo.subsystem != SDL_SYSWM_WINDOWS)
    {
        if (errorMessage != nullptr)
        {
            *errorMessage = std::string{"SDL_GetWindowWMInfo failed for D3D12: "} + SDL_GetError();
        }
        return false;
    }

    _window = window;
    _windowHandle = windowInfo.info.win.window;
    RefreshDrawableSize();
    _swapChainWidth = static_cast<std::uint32_t>(std::max(_drawableWidth, 1));
    _swapChainHeight = static_cast<std::uint32_t>(std::max(_drawableHeight, 1));

    if (!CreateDeviceAndQueue(errorMessage) ||
        !CreateSwapChain(errorMessage) ||
        !CreateDescriptorHeaps(errorMessage) ||
        !CreateFrameResources(errorMessage) ||
        !CreateRenderTargets(errorMessage) ||
        !CreateTimestampResources(errorMessage))
    {
        Shutdown();
        return false;
    }

    _initialized = true;
    std::cout << "Graphics backend: D3D12\n";
    std::cout << "D3D12 adapter: " << _adapterName << '\n';
    std::cout << "D3D12 mode: " << _versionString << '\n';
    return true;
}

bool D3D12GraphicsBackend::InitializeImGui(Gui::ImGuiLayer& guiLayer, std::string* errorMessage)
{
    _imguiFontDescriptor = AllocateSrvDescriptor();
    if (!_imguiFontDescriptor.IsValid())
    {
        if (errorMessage != nullptr)
        {
            *errorMessage = "D3D12 SRV heap has no descriptor available for Dear ImGui";
        }
        return false;
    }

    const Gui::ImGuiD3D12BackendConfig config{
        .Window = _window,
        .Device = _device.Get(),
        .CommandQueue = _commandQueue.Get(),
        .SrvDescriptorHeap = _srvHeap.Get(),
        .FontSrvCpuDescriptor = _imguiFontDescriptor.Cpu,
        .FontSrvGpuDescriptor = _imguiFontDescriptor.Gpu,
        .RenderTargetFormat = BackBufferFormat,
        .DepthStencilFormat = DepthBufferFormat,
        .FramesInFlight = static_cast<int>(FrameCount),
    };
    if (guiLayer.Initialize(config))
    {
        return true;
    }

    ReleaseSrvDescriptor(_imguiFontDescriptor);
    if (errorMessage != nullptr)
    {
        *errorMessage = "Dear ImGui D3D12 backend initialization failed";
    }
    return false;
}

void D3D12GraphicsBackend::WaitForGpuIdle()
{
    if (_commandQueue == nullptr || _fence == nullptr || _fenceEvent == nullptr)
    {
        return;
    }
    std::string errorMessage;
    if (!SignalAndWait(&errorMessage) && !errorMessage.empty())
    {
        std::cerr << errorMessage << '\n';
    }
}

void D3D12GraphicsBackend::Shutdown()
{
    if (_commandQueue != nullptr && _fence != nullptr && _fenceEvent != nullptr)
    {
        std::string ignoredError;
        if (!SignalAndWait(&ignoredError) && !ignoredError.empty())
        {
            std::cerr << ignoredError << '\n';
        }
    }

    _frameOpen = false;
    ReleaseSrvDescriptor(_imguiFontDescriptor);
    ReleaseRenderTargets();
    _timestampReadback.Reset();
    _timestampQueryHeap.Reset();
    _commandList.Reset();
    for (FrameResource& frame : _frames)
    {
        frame.CommandAllocator.Reset();
        frame.FenceValue = 0;
    }
    _fence.Reset();
    if (_fenceEvent != nullptr)
    {
        CloseHandle(_fenceEvent);
        _fenceEvent = nullptr;
    }
    _srvHeap.Reset();
    _dsvHeap.Reset();
    _rtvHeap.Reset();
    _swapChain.Reset();
    _commandQueue.Reset();
    _device.Reset();
    _adapter.Reset();
    _factory.Reset();
    _freeSrvIndices.clear();
    _window = nullptr;
    _windowHandle = nullptr;
    _drawableWidth = 0;
    _drawableHeight = 0;
    _swapChainWidth = 0;
    _swapChainHeight = 0;
    _frameIndex = 0;
    _nextFenceValue = 1;
    _lastGpuFrameMilliseconds = 0.0F;
    _lastGpuWaitMilliseconds = 0.0F;
    _adapterName.clear();
    _initialized = false;
}

void D3D12GraphicsBackend::BeginFrame()
{
    if (!_initialized || _frameOpen)
    {
        return;
    }

    RefreshDrawableSize();
    if (_drawableWidth <= 0 || _drawableHeight <= 0)
    {
        return;
    }

    if (_swapChainWidth != static_cast<std::uint32_t>(_drawableWidth) ||
        _swapChainHeight != static_cast<std::uint32_t>(_drawableHeight))
    {
        std::string resizeError;
        if (!ResizeSwapChain(
                static_cast<std::uint32_t>(_drawableWidth),
                static_cast<std::uint32_t>(_drawableHeight),
                &resizeError))
        {
            std::cerr << resizeError << '\n';
            return;
        }
    }

    std::string waitError;
    if (!WaitForFrame(_frameIndex, &waitError))
    {
        std::cerr << waitError << '\n';
        return;
    }
    ReadCompletedTimestamp(_frameIndex);

    FrameResource& frame = _frames[_frameIndex];
    HRESULT result = frame.CommandAllocator->Reset();
    if (FAILED(result))
    {
        ReportFailure("ID3D12CommandAllocator::Reset", result);
        return;
    }
    result = _commandList->Reset(frame.CommandAllocator.Get(), nullptr);
    if (FAILED(result))
    {
        ReportFailure("ID3D12GraphicsCommandList::Reset", result);
        return;
    }

    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = _backBuffers[_frameIndex].Get();
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    _commandList->ResourceBarrier(1, &barrier);

    D3D12_CPU_DESCRIPTOR_HANDLE rtv = _rtvHeap->GetCPUDescriptorHandleForHeapStart();
    rtv.ptr += static_cast<SIZE_T>(_frameIndex) * _rtvDescriptorSize;
    const D3D12_CPU_DESCRIPTOR_HANDLE dsv = _dsvHeap->GetCPUDescriptorHandleForHeapStart();
    _commandList->OMSetRenderTargets(1, &rtv, FALSE, &dsv);
    constexpr std::array<float, 4> ClearColor{0.035F, 0.045F, 0.055F, 1.0F};
    _commandList->ClearRenderTargetView(rtv, ClearColor.data(), 0, nullptr);
    _commandList->ClearDepthStencilView(dsv, D3D12_CLEAR_FLAG_DEPTH, 1.0F, 0, 0, nullptr);

    const D3D12_VIEWPORT viewport{
        0.0F,
        0.0F,
        static_cast<float>(_swapChainWidth),
        static_cast<float>(_swapChainHeight),
        0.0F,
        1.0F,
    };
    const D3D12_RECT scissor{0, 0, static_cast<LONG>(_swapChainWidth), static_cast<LONG>(_swapChainHeight)};
    _commandList->RSSetViewports(1, &viewport);
    _commandList->RSSetScissorRects(1, &scissor);
    ID3D12DescriptorHeap* heaps[] = {_srvHeap.Get()};
    _commandList->SetDescriptorHeaps(1, heaps);

    if (_timestampQueryHeap != nullptr)
    {
        _commandList->EndQuery(_timestampQueryHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, _frameIndex * 2U);
    }
    _frameOpen = true;
}

void D3D12GraphicsBackend::BeginImGuiFrame(Gui::ImGuiLayer& guiLayer)
{
    guiLayer.BeginFrame();
}

void D3D12GraphicsBackend::RenderImGui(Gui::ImGuiLayer& guiLayer)
{
    if (_frameOpen)
    {
        guiLayer.EndFrame(_commandList.Get());
    }
}

void D3D12GraphicsBackend::Present()
{
    if (!_frameOpen)
    {
        return;
    }

    if (_timestampQueryHeap != nullptr && _timestampReadback != nullptr)
    {
        const std::uint32_t queryStart = _frameIndex * 2U;
        _commandList->EndQuery(_timestampQueryHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, queryStart + 1U);
        _commandList->ResolveQueryData(
            _timestampQueryHeap.Get(),
            D3D12_QUERY_TYPE_TIMESTAMP,
            queryStart,
            2,
            _timestampReadback.Get(),
            static_cast<std::uint64_t>(queryStart) * sizeof(std::uint64_t));
    }

    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = _backBuffers[_frameIndex].Get();
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    _commandList->ResourceBarrier(1, &barrier);

    HRESULT result = _commandList->Close();
    if (FAILED(result))
    {
        ReportFailure("ID3D12GraphicsCommandList::Close", result);
        _frameOpen = false;
        return;
    }

    ID3D12CommandList* lists[] = {_commandList.Get()};
    _commandQueue->ExecuteCommandLists(1, lists);

    const UINT syncInterval = _vSyncEnabled ? 1U : 0U;
    const UINT presentFlags = !_vSyncEnabled && _tearingSupported ? DXGI_PRESENT_ALLOW_TEARING : 0U;
    result = _swapChain->Present(syncInterval, presentFlags);
    if (FAILED(result))
    {
        ReportFailure("IDXGISwapChain::Present", result);
    }

    const std::uint64_t fenceValue = _nextFenceValue++;
    result = _commandQueue->Signal(_fence.Get(), fenceValue);
    if (FAILED(result))
    {
        ReportFailure("ID3D12CommandQueue::Signal", result);
    }
    else
    {
        _frames[_frameIndex].FenceValue = fenceValue;
    }

    _frameIndex = _swapChain->GetCurrentBackBufferIndex();
    _frameOpen = false;
}

void D3D12GraphicsBackend::RefreshDrawableSize()
{
    if (_window == nullptr)
    {
        _drawableWidth = 0;
        _drawableHeight = 0;
        return;
    }
    SDL_GetWindowSizeInPixels(_window, &_drawableWidth, &_drawableHeight);
}

bool D3D12GraphicsBackend::SetVSyncEnabled(bool enabled)
{
    _vSyncEnabled = enabled;
    return true;
}

bool D3D12GraphicsBackend::VSyncEnabled() const
{
    return _vSyncEnabled;
}

int D3D12GraphicsBackend::DrawableWidth() const
{
    return _drawableWidth;
}

int D3D12GraphicsBackend::DrawableHeight() const
{
    return _drawableHeight;
}

bool D3D12GraphicsBackend::UsesZeroToOneDepth() const
{
    return true;
}

const std::string& D3D12GraphicsBackend::AdapterName() const
{
    return _adapterName;
}

const std::string& D3D12GraphicsBackend::VersionString() const
{
    return _versionString;
}

bool D3D12GraphicsBackend::SupportsGpuRoamLike() const
{
    return true;
}

float D3D12GraphicsBackend::LastGpuFrameMilliseconds() const
{
    return _lastGpuFrameMilliseconds;
}

float D3D12GraphicsBackend::LastGpuWaitMilliseconds() const
{
    return _lastGpuWaitMilliseconds;
}

bool D3D12GraphicsBackend::IsValid() const
{
    return _initialized && _device != nullptr && _swapChain != nullptr;
}

ID3D12Device* D3D12GraphicsBackend::Device() const
{
    return _device.Get();
}

ID3D12CommandQueue* D3D12GraphicsBackend::CommandQueue() const
{
    return _commandQueue.Get();
}

ID3D12GraphicsCommandList* D3D12GraphicsBackend::CommandList() const
{
    return _frameOpen ? _commandList.Get() : nullptr;
}

ID3D12DescriptorHeap* D3D12GraphicsBackend::ShaderVisibleSrvHeap() const
{
    return _srvHeap.Get();
}

DXGI_FORMAT D3D12GraphicsBackend::RenderTargetFormat() const
{
    return BackBufferFormat;
}

DXGI_FORMAT D3D12GraphicsBackend::DepthStencilFormat() const
{
    return DepthBufferFormat;
}

std::uint32_t D3D12GraphicsBackend::CurrentFrameIndex() const
{
    return _frameIndex;
}

bool D3D12GraphicsBackend::FrameOpen() const
{
    return _frameOpen;
}

D3D12DescriptorAllocation D3D12GraphicsBackend::AllocateSrvDescriptor()
{
    if (_srvHeap == nullptr || _freeSrvIndices.empty())
    {
        return {};
    }

    D3D12DescriptorAllocation allocation{};
    allocation.Index = _freeSrvIndices.back();
    _freeSrvIndices.pop_back();
    allocation.Cpu = _srvHeap->GetCPUDescriptorHandleForHeapStart();
    allocation.Gpu = _srvHeap->GetGPUDescriptorHandleForHeapStart();
    allocation.Cpu.ptr += static_cast<SIZE_T>(allocation.Index) * _srvDescriptorSize;
    allocation.Gpu.ptr += static_cast<UINT64>(allocation.Index) * _srvDescriptorSize;
    return allocation;
}

void D3D12GraphicsBackend::ReleaseSrvDescriptor(D3D12DescriptorAllocation& allocation)
{
    if (!allocation.IsValid())
    {
        return;
    }
    _freeSrvIndices.push_back(allocation.Index);
    allocation = {};
}

bool D3D12GraphicsBackend::ExecuteImmediate(
    const std::function<bool(ID3D12GraphicsCommandList*, std::string*)>& recorder,
    std::string* errorMessage)
{
    Microsoft::WRL::ComPtr<ID3D12CommandAllocator> allocator;
    HRESULT result = _device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocator));
    if (FAILED(result))
    {
        SetError(errorMessage, "CreateCommandAllocator for immediate upload", result);
        return false;
    }

    Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> commandList;
    result = _device->CreateCommandList(
        0,
        D3D12_COMMAND_LIST_TYPE_DIRECT,
        allocator.Get(),
        nullptr,
        IID_PPV_ARGS(&commandList));
    if (FAILED(result))
    {
        SetError(errorMessage, "CreateCommandList for immediate upload", result);
        return false;
    }

    if (!recorder(commandList.Get(), errorMessage))
    {
        return false;
    }

    result = commandList->Close();
    if (FAILED(result))
    {
        SetError(errorMessage, "Close immediate command list", result);
        return false;
    }
    ID3D12CommandList* lists[] = {commandList.Get()};
    _commandQueue->ExecuteCommandLists(1, lists);
    return SignalAndWait(errorMessage);
}

bool D3D12GraphicsBackend::CreateDeviceAndQueue(std::string* errorMessage)
{
    UINT factoryFlags = 0;
#if !defined(NDEBUG)
    Microsoft::WRL::ComPtr<ID3D12Debug> debugController;
    if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debugController))))
    {
        debugController->EnableDebugLayer();
        factoryFlags |= DXGI_CREATE_FACTORY_DEBUG;
    }
#endif

    HRESULT result = CreateDXGIFactory2(factoryFlags, IID_PPV_ARGS(&_factory));
    if (FAILED(result))
    {
        SetError(errorMessage, "CreateDXGIFactory2", result);
        return false;
    }

    Microsoft::WRL::ComPtr<IDXGIFactory5> factory5;
    if (SUCCEEDED(_factory.As(&factory5)))
    {
        BOOL allowTearing = FALSE;
        if (SUCCEEDED(factory5->CheckFeatureSupport(
                DXGI_FEATURE_PRESENT_ALLOW_TEARING,
                &allowTearing,
                sizeof(allowTearing))))
        {
            _tearingSupported = allowTearing == TRUE;
        }
    }

    for (UINT adapterIndex = 0;; ++adapterIndex)
    {
        Microsoft::WRL::ComPtr<IDXGIAdapter1> candidate;
        result = _factory->EnumAdapterByGpuPreference(
            adapterIndex,
            DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE,
            IID_PPV_ARGS(&candidate));
        if (result == DXGI_ERROR_NOT_FOUND)
        {
            break;
        }
        if (FAILED(result))
        {
            continue;
        }

        DXGI_ADAPTER_DESC1 description{};
        candidate->GetDesc1(&description);
        if ((description.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) != 0)
        {
            continue;
        }
        if (SUCCEEDED(D3D12CreateDevice(candidate.Get(), D3D_FEATURE_LEVEL_12_0, __uuidof(ID3D12Device), nullptr)))
        {
            _adapter = candidate;
            _adapterName = WideToUtf8(description.Description);
            break;
        }
    }

    if (_adapter == nullptr)
    {
        if (errorMessage != nullptr)
        {
            *errorMessage = "No hardware adapter supports D3D12 feature level 12_0";
        }
        return false;
    }

    LARGE_INTEGER driverVersion{};
    if (SUCCEEDED(_adapter->CheckInterfaceSupport(__uuidof(IDXGIDevice), &driverVersion)))
    {
        std::ostringstream versionStream;
        versionStream << "Direct3D 12 (feature level 12_0); driver "
                      << HIWORD(driverVersion.HighPart) << '.'
                      << LOWORD(driverVersion.HighPart) << '.'
                      << HIWORD(driverVersion.LowPart) << '.'
                      << LOWORD(driverVersion.LowPart);
        _versionString = versionStream.str();
    }

    result = D3D12CreateDevice(_adapter.Get(), D3D_FEATURE_LEVEL_12_0, IID_PPV_ARGS(&_device));
    if (FAILED(result))
    {
        SetError(errorMessage, "D3D12CreateDevice", result);
        return false;
    }

#if !defined(NDEBUG)
    Microsoft::WRL::ComPtr<ID3D12InfoQueue> infoQueue;
    if (SUCCEEDED(_device.As(&infoQueue)))
    {
        infoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_CORRUPTION, TRUE);
        infoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_ERROR, TRUE);
    }
#endif

    D3D12_COMMAND_QUEUE_DESC queueDescription{};
    queueDescription.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    queueDescription.Priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL;
    result = _device->CreateCommandQueue(&queueDescription, IID_PPV_ARGS(&_commandQueue));
    if (FAILED(result))
    {
        SetError(errorMessage, "CreateCommandQueue", result);
        return false;
    }
    return true;
}

bool D3D12GraphicsBackend::CreateSwapChain(std::string* errorMessage)
{
    DXGI_SWAP_CHAIN_DESC1 description{};
    description.Width = _swapChainWidth;
    description.Height = _swapChainHeight;
    description.Format = BackBufferFormat;
    description.SampleDesc.Count = 1;
    description.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    description.BufferCount = FrameCount;
    description.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    description.Flags = _tearingSupported ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0U;

    Microsoft::WRL::ComPtr<IDXGISwapChain1> swapChain;
    HRESULT result = _factory->CreateSwapChainForHwnd(
        _commandQueue.Get(),
        _windowHandle,
        &description,
        nullptr,
        nullptr,
        &swapChain);
    if (FAILED(result))
    {
        SetError(errorMessage, "CreateSwapChainForHwnd", result);
        return false;
    }
    _factory->MakeWindowAssociation(_windowHandle, DXGI_MWA_NO_ALT_ENTER);
    result = swapChain.As(&_swapChain);
    if (FAILED(result))
    {
        SetError(errorMessage, "Query IDXGISwapChain3", result);
        return false;
    }
    _frameIndex = _swapChain->GetCurrentBackBufferIndex();
    return true;
}

bool D3D12GraphicsBackend::CreateDescriptorHeaps(std::string* errorMessage)
{
    D3D12_DESCRIPTOR_HEAP_DESC rtvDescription{};
    rtvDescription.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    rtvDescription.NumDescriptors = FrameCount;
    HRESULT result = _device->CreateDescriptorHeap(&rtvDescription, IID_PPV_ARGS(&_rtvHeap));
    if (FAILED(result))
    {
        SetError(errorMessage, "Create RTV descriptor heap", result);
        return false;
    }

    D3D12_DESCRIPTOR_HEAP_DESC dsvDescription{};
    dsvDescription.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
    dsvDescription.NumDescriptors = 1;
    result = _device->CreateDescriptorHeap(&dsvDescription, IID_PPV_ARGS(&_dsvHeap));
    if (FAILED(result))
    {
        SetError(errorMessage, "Create DSV descriptor heap", result);
        return false;
    }

    D3D12_DESCRIPTOR_HEAP_DESC srvDescription{};
    srvDescription.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    srvDescription.NumDescriptors = SrvDescriptorCount;
    srvDescription.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    result = _device->CreateDescriptorHeap(&srvDescription, IID_PPV_ARGS(&_srvHeap));
    if (FAILED(result))
    {
        SetError(errorMessage, "Create shader-visible SRV descriptor heap", result);
        return false;
    }

    _rtvDescriptorSize = _device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    _srvDescriptorSize = _device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    _freeSrvIndices.reserve(SrvDescriptorCount);
    for (std::uint32_t index = SrvDescriptorCount; index > 0; --index)
    {
        _freeSrvIndices.push_back(index - 1U);
    }
    return true;
}

bool D3D12GraphicsBackend::CreateFrameResources(std::string* errorMessage)
{
    for (FrameResource& frame : _frames)
    {
        const HRESULT result = _device->CreateCommandAllocator(
            D3D12_COMMAND_LIST_TYPE_DIRECT,
            IID_PPV_ARGS(&frame.CommandAllocator));
        if (FAILED(result))
        {
            SetError(errorMessage, "Create frame command allocator", result);
            return false;
        }
    }

    HRESULT result = _device->CreateCommandList(
        0,
        D3D12_COMMAND_LIST_TYPE_DIRECT,
        _frames[0].CommandAllocator.Get(),
        nullptr,
        IID_PPV_ARGS(&_commandList));
    if (FAILED(result))
    {
        SetError(errorMessage, "Create graphics command list", result);
        return false;
    }
    result = _commandList->Close();
    if (FAILED(result))
    {
        SetError(errorMessage, "Close initial graphics command list", result);
        return false;
    }

    result = _device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&_fence));
    if (FAILED(result))
    {
        SetError(errorMessage, "Create frame fence", result);
        return false;
    }
    _fenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
    if (_fenceEvent == nullptr)
    {
        if (errorMessage != nullptr)
        {
            *errorMessage = "CreateEvent failed for D3D12 frame fence";
        }
        return false;
    }
    return true;
}

bool D3D12GraphicsBackend::CreateRenderTargets(std::string* errorMessage)
{
    D3D12_CPU_DESCRIPTOR_HANDLE rtv = _rtvHeap->GetCPUDescriptorHandleForHeapStart();
    for (std::uint32_t index = 0; index < FrameCount; ++index)
    {
        HRESULT result = _swapChain->GetBuffer(index, IID_PPV_ARGS(&_backBuffers[index]));
        if (FAILED(result))
        {
            SetError(errorMessage, "Get swap-chain back buffer", result);
            return false;
        }
        _device->CreateRenderTargetView(_backBuffers[index].Get(), nullptr, rtv);
        rtv.ptr += _rtvDescriptorSize;
    }

    D3D12_RESOURCE_DESC depthDescription{};
    depthDescription.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    depthDescription.Width = _swapChainWidth;
    depthDescription.Height = _swapChainHeight;
    depthDescription.DepthOrArraySize = 1;
    depthDescription.MipLevels = 1;
    depthDescription.Format = DepthBufferFormat;
    depthDescription.SampleDesc.Count = 1;
    depthDescription.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    depthDescription.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
    const D3D12_HEAP_PROPERTIES heapProperties = HeapProperties(D3D12_HEAP_TYPE_DEFAULT);
    D3D12_CLEAR_VALUE clearValue{};
    clearValue.Format = DepthBufferFormat;
    clearValue.DepthStencil.Depth = 1.0F;
    HRESULT result = _device->CreateCommittedResource(
        &heapProperties,
        D3D12_HEAP_FLAG_NONE,
        &depthDescription,
        D3D12_RESOURCE_STATE_DEPTH_WRITE,
        &clearValue,
        IID_PPV_ARGS(&_depthBuffer));
    if (FAILED(result))
    {
        SetError(errorMessage, "Create D3D12 depth buffer", result);
        return false;
    }
    _device->CreateDepthStencilView(_depthBuffer.Get(), nullptr, _dsvHeap->GetCPUDescriptorHandleForHeapStart());
    return true;
}

bool D3D12GraphicsBackend::CreateTimestampResources(std::string* errorMessage)
{
    D3D12_QUERY_HEAP_DESC queryDescription{};
    queryDescription.Type = D3D12_QUERY_HEAP_TYPE_TIMESTAMP;
    queryDescription.Count = FrameCount * 2U;
    HRESULT result = _device->CreateQueryHeap(&queryDescription, IID_PPV_ARGS(&_timestampQueryHeap));
    if (FAILED(result))
    {
        SetError(errorMessage, "Create D3D12 timestamp query heap", result);
        return false;
    }

    D3D12_RESOURCE_DESC readbackDescription{};
    readbackDescription.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    readbackDescription.Width = FrameCount * 2U * sizeof(std::uint64_t);
    readbackDescription.Height = 1;
    readbackDescription.DepthOrArraySize = 1;
    readbackDescription.MipLevels = 1;
    readbackDescription.SampleDesc.Count = 1;
    readbackDescription.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    const D3D12_HEAP_PROPERTIES heapProperties = HeapProperties(D3D12_HEAP_TYPE_READBACK);
    result = _device->CreateCommittedResource(
        &heapProperties,
        D3D12_HEAP_FLAG_NONE,
        &readbackDescription,
        D3D12_RESOURCE_STATE_COPY_DEST,
        nullptr,
        IID_PPV_ARGS(&_timestampReadback));
    if (FAILED(result))
    {
        SetError(errorMessage, "Create D3D12 timestamp readback buffer", result);
        return false;
    }

    result = _commandQueue->GetTimestampFrequency(&_timestampFrequency);
    if (FAILED(result))
    {
        SetError(errorMessage, "Get D3D12 timestamp frequency", result);
        return false;
    }
    return true;
}

bool D3D12GraphicsBackend::ResizeSwapChain(std::uint32_t width, std::uint32_t height, std::string* errorMessage)
{
    if (width == 0 || height == 0)
    {
        return true;
    }
    if (!SignalAndWait(errorMessage))
    {
        return false;
    }
    ReleaseRenderTargets();
    const UINT flags = _tearingSupported ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0U;
    const HRESULT result = _swapChain->ResizeBuffers(FrameCount, width, height, BackBufferFormat, flags);
    if (FAILED(result))
    {
        SetError(errorMessage, "Resize D3D12 swap chain", result);
        return false;
    }
    _swapChainWidth = width;
    _swapChainHeight = height;
    _frameIndex = _swapChain->GetCurrentBackBufferIndex();
    return CreateRenderTargets(errorMessage);
}

bool D3D12GraphicsBackend::WaitForFrame(std::uint32_t frameIndex, std::string* errorMessage)
{
    _lastGpuWaitMilliseconds = 0.0F;
    const std::uint64_t fenceValue = _frames[frameIndex].FenceValue;
    if (fenceValue == 0 || _fence->GetCompletedValue() >= fenceValue)
    {
        return true;
    }
    HRESULT result = _fence->SetEventOnCompletion(fenceValue, _fenceEvent);
    if (FAILED(result))
    {
        SetError(errorMessage, "Set frame fence event", result);
        return false;
    }
    const auto waitStart = std::chrono::steady_clock::now();
    WaitForSingleObject(_fenceEvent, INFINITE);
    _lastGpuWaitMilliseconds =
        std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - waitStart).count();
    return true;
}

bool D3D12GraphicsBackend::SignalAndWait(std::string* errorMessage)
{
    const std::uint64_t fenceValue = _nextFenceValue++;
    HRESULT result = _commandQueue->Signal(_fence.Get(), fenceValue);
    if (FAILED(result))
    {
        SetError(errorMessage, "Signal D3D12 fence", result);
        return false;
    }
    result = _fence->SetEventOnCompletion(fenceValue, _fenceEvent);
    if (FAILED(result))
    {
        SetError(errorMessage, "Set D3D12 fence event", result);
        return false;
    }
    WaitForSingleObject(_fenceEvent, INFINITE);
    return true;
}

void D3D12GraphicsBackend::ReadCompletedTimestamp(std::uint32_t frameIndex)
{
    if (_timestampReadback == nullptr || _timestampFrequency == 0 || _frames[frameIndex].FenceValue == 0)
    {
        return;
    }

    const std::uint64_t offset = static_cast<std::uint64_t>(frameIndex) * 2U * sizeof(std::uint64_t);
    D3D12_RANGE readRange{static_cast<SIZE_T>(offset), static_cast<SIZE_T>(offset + 2U * sizeof(std::uint64_t))};
    void* mappedData = nullptr;
    if (SUCCEEDED(_timestampReadback->Map(0, &readRange, &mappedData)))
    {
        const auto* timestamps = static_cast<const std::uint64_t*>(mappedData);
        const std::uint64_t* pair = timestamps + frameIndex * 2U;
        if (pair[1] >= pair[0])
        {
            _lastGpuFrameMilliseconds =
                static_cast<float>(static_cast<double>(pair[1] - pair[0]) * 1000.0 /
                                   static_cast<double>(_timestampFrequency));
        }
        const D3D12_RANGE writeRange{0, 0};
        _timestampReadback->Unmap(0, &writeRange);
    }
}

void D3D12GraphicsBackend::ReleaseRenderTargets()
{
    _depthBuffer.Reset();
    for (auto& backBuffer : _backBuffers)
    {
        backBuffer.Reset();
    }
}

void D3D12GraphicsBackend::ReportFailure(const char* operation, HRESULT result) const
{
    std::cerr << operation << " failed: " << HResultText(result);
    if (_device != nullptr)
    {
        const HRESULT removedReason = _device->GetDeviceRemovedReason();
        if (FAILED(removedReason))
        {
            std::cerr << "\nD3D12 device removed reason: " << HResultText(removedReason);
        }
    }
    std::cerr << '\n';
}
} // namespace ParallelRoam::Render
