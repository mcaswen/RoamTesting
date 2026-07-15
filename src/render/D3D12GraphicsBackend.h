#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include "render/GraphicsBackend.h"

#include <SDL.h>

#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl/client.h>

#include <array>
#include <cstdint>
#include <functional>
#include <limits>
#include <string>
#include <vector>

namespace ParallelRoam::Render
{
struct D3D12DescriptorAllocation
{
    std::uint32_t Index{std::numeric_limits<std::uint32_t>::max()};
    D3D12_CPU_DESCRIPTOR_HANDLE Cpu{};
    D3D12_GPU_DESCRIPTOR_HANDLE Gpu{};

    [[nodiscard]] bool IsValid() const
    {
        return Index != std::numeric_limits<std::uint32_t>::max();
    }
};

class D3D12GraphicsBackend final : public IGraphicsBackend
{
public:
    static constexpr std::uint32_t FrameCount = 2;

    D3D12GraphicsBackend() = default;
    ~D3D12GraphicsBackend() override;

    D3D12GraphicsBackend(const D3D12GraphicsBackend&) = delete;
    D3D12GraphicsBackend& operator=(const D3D12GraphicsBackend&) = delete;

    [[nodiscard]] GraphicsApi Api() const override;
    [[nodiscard]] const char* Name() const override;
    [[nodiscard]] bool ConfigureWindow(std::string* errorMessage) override;
    [[nodiscard]] std::uint32_t RequiredSdlWindowFlags() const override;
    [[nodiscard]] bool Initialize(SDL_Window* window, std::string* errorMessage) override;
    [[nodiscard]] bool InitializeImGui(Gui::ImGuiLayer& guiLayer, std::string* errorMessage) override;
    void WaitForGpuIdle() override;
    void Shutdown() override;

    void BeginFrame() override;
    void BeginImGuiFrame(Gui::ImGuiLayer& guiLayer) override;
    void RenderImGui(Gui::ImGuiLayer& guiLayer) override;
    void Present() override;
    void RefreshDrawableSize() override;

    [[nodiscard]] bool SetVSyncEnabled(bool enabled) override;
    [[nodiscard]] bool VSyncEnabled() const override;
    [[nodiscard]] int DrawableWidth() const override;
    [[nodiscard]] int DrawableHeight() const override;
    [[nodiscard]] bool UsesZeroToOneDepth() const override;
    [[nodiscard]] const std::string& AdapterName() const override;
    [[nodiscard]] const std::string& VersionString() const override;
    [[nodiscard]] bool SupportsGpuRoamLike() const override;
    [[nodiscard]] float LastGpuFrameMilliseconds() const override;
    [[nodiscard]] float LastGpuWaitMilliseconds() const override;
    [[nodiscard]] bool IsValid() const override;

    [[nodiscard]] ID3D12Device* Device() const;
    [[nodiscard]] ID3D12CommandQueue* CommandQueue() const;
    [[nodiscard]] ID3D12GraphicsCommandList* CommandList() const;
    [[nodiscard]] ID3D12DescriptorHeap* ShaderVisibleSrvHeap() const;
    [[nodiscard]] DXGI_FORMAT RenderTargetFormat() const;
    [[nodiscard]] DXGI_FORMAT DepthStencilFormat() const;
    [[nodiscard]] std::uint32_t CurrentFrameIndex() const;
    [[nodiscard]] bool FrameOpen() const;

    [[nodiscard]] D3D12DescriptorAllocation AllocateSrvDescriptor();
    void ReleaseSrvDescriptor(D3D12DescriptorAllocation& allocation);
    [[nodiscard]] bool ExecuteImmediate(
        const std::function<bool(ID3D12GraphicsCommandList*, std::string*)>& recorder,
        std::string* errorMessage);

private:
    struct FrameResource
    {
        Microsoft::WRL::ComPtr<ID3D12CommandAllocator> CommandAllocator;
        std::uint64_t FenceValue{0};
    };

    [[nodiscard]] bool CreateDeviceAndQueue(std::string* errorMessage);
    [[nodiscard]] bool CreateSwapChain(std::string* errorMessage);
    [[nodiscard]] bool CreateDescriptorHeaps(std::string* errorMessage);
    [[nodiscard]] bool CreateFrameResources(std::string* errorMessage);
    [[nodiscard]] bool CreateRenderTargets(std::string* errorMessage);
    [[nodiscard]] bool CreateTimestampResources(std::string* errorMessage);
    [[nodiscard]] bool ResizeSwapChain(std::uint32_t width, std::uint32_t height, std::string* errorMessage);
    [[nodiscard]] bool WaitForFrame(std::uint32_t frameIndex, std::string* errorMessage);
    [[nodiscard]] bool SignalAndWait(std::string* errorMessage);
    void ReadCompletedTimestamp(std::uint32_t frameIndex);
    void ReleaseRenderTargets();
    void ReportFailure(const char* operation, HRESULT result) const;

    SDL_Window* _window{nullptr};
    HWND _windowHandle{nullptr};
    Microsoft::WRL::ComPtr<IDXGIFactory6> _factory;
    Microsoft::WRL::ComPtr<IDXGIAdapter1> _adapter;
    Microsoft::WRL::ComPtr<ID3D12Device> _device;
    Microsoft::WRL::ComPtr<ID3D12CommandQueue> _commandQueue;
    Microsoft::WRL::ComPtr<IDXGISwapChain3> _swapChain;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> _rtvHeap;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> _dsvHeap;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> _srvHeap;
    std::array<Microsoft::WRL::ComPtr<ID3D12Resource>, FrameCount> _backBuffers;
    Microsoft::WRL::ComPtr<ID3D12Resource> _depthBuffer;
    std::array<FrameResource, FrameCount> _frames;
    Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> _commandList;
    Microsoft::WRL::ComPtr<ID3D12Fence> _fence;
    HANDLE _fenceEvent{nullptr};
    std::uint64_t _nextFenceValue{1};
    Microsoft::WRL::ComPtr<ID3D12QueryHeap> _timestampQueryHeap;
    Microsoft::WRL::ComPtr<ID3D12Resource> _timestampReadback;
    std::uint64_t _timestampFrequency{0};
    float _lastGpuFrameMilliseconds{0.0F};
    float _lastGpuWaitMilliseconds{0.0F};
    D3D12DescriptorAllocation _imguiFontDescriptor;
    std::vector<std::uint32_t> _freeSrvIndices;
    std::uint32_t _rtvDescriptorSize{0};
    std::uint32_t _srvDescriptorSize{0};
    std::uint32_t _frameIndex{0};
    std::uint32_t _swapChainWidth{0};
    std::uint32_t _swapChainHeight{0};
    int _drawableWidth{0};
    int _drawableHeight{0};
    bool _vSyncEnabled{false};
    bool _tearingSupported{false};
    bool _frameOpen{false};
    bool _initialized{false};
    std::string _adapterName;
    std::string _versionString{"Direct3D 12 (feature level 12_0)"};
};
} // namespace ParallelRoam::Render
