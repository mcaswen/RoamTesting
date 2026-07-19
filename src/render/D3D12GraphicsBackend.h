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
/// <summary>
/// shader-visible 描述符堆中的稳定槽位
/// </summary>
struct D3D12DescriptorAllocation
{
    // Index 是释放时归还空闲表的唯一身份
    std::uint32_t Index{std::numeric_limits<std::uint32_t>::max()}; // 无效值不占用堆槽位
    // CPU 句柄用于创建设备视图
    D3D12_CPU_DESCRIPTOR_HANDLE Cpu{}; // 与 Index 指向同一描述符
    // GPU 句柄用于根描述符表绑定
    D3D12_GPU_DESCRIPTOR_HANDLE Gpu{}; // 仅 shader-visible 堆可使用

    [[nodiscard]] bool IsValid() const
    {
        return Index != std::numeric_limits<std::uint32_t>::max();
    }
};

/// <summary>
/// 管理 D3D12 设备、交换链、帧资源和提交同步
/// </summary>
class D3D12GraphicsBackend final : public IGraphicsBackend
{
public:
    // 每个交换链缓冲拥有独立命令分配器和围栏值
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
    [[nodiscard]] const GraphicsDeviceCapabilities& GraphicsCapabilities() const override;
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
    /// <summary>
    /// 与单个交换链缓冲绑定的命令分配器和完成值
    /// </summary>
    struct FrameResource
    {
        // 只有对应 FenceValue 完成后才能 Reset
        Microsoft::WRL::ComPtr<ID3D12CommandAllocator> CommandAllocator; // 当前交换链帧独占
        // 零表示该帧资源尚未提交过
        std::uint64_t FenceValue{0}; // 最近一次使用该 allocator 的提交序列
    };

    [[nodiscard]] bool CreateDeviceAndQueue(std::string* errorMessage);
    void QueryDeviceCapabilities();
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

    // SDL 和原生窗口只借用，不负责销毁
    SDL_Window* _window{nullptr}; // SDL 窗口借用指针
    HWND _windowHandle{nullptr}; // 创建交换链使用的原生句柄

    // 设备级对象按 Shutdown 中的逆序释放
    Microsoft::WRL::ComPtr<IDXGIFactory6> _factory; // DXGI 适配器和交换链入口
    Microsoft::WRL::ComPtr<IDXGIAdapter1> _adapter; // 当前选中的硬件适配器
    Microsoft::WRL::ComPtr<ID3D12Device> _device; // 所有 D3D12 资源的创建设备
    Microsoft::WRL::ComPtr<ID3D12CommandQueue> _commandQueue; // 唯一 direct queue
    Microsoft::WRL::ComPtr<IDXGISwapChain3> _swapChain; // 双缓冲交换链

    // RTV/DSV 为 CPU 可见，SRV 堆同时对 shader 可见
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> _rtvHeap; // 每个 back buffer 一个 RTV
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> _dsvHeap; // 共享深度目标的 DSV
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> _srvHeap; // ImGui 和业务 SRV 共用的可见堆

    // 交换链尺寸变化时只重建这一组渲染目标资源
    std::array<Microsoft::WRL::ComPtr<ID3D12Resource>, FrameCount> _backBuffers; // 交换链拥有的颜色目标引用
    Microsoft::WRL::ComPtr<ID3D12Resource> _depthBuffer; // 与 drawable 尺寸一致的深度目标

    // 命令列表由当前 frame allocator 驱动
    std::array<FrameResource, FrameCount> _frames; // 按 back buffer 索引隔离的 allocator
    Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> _commandList; // 主循环复用的 direct list

    // 单一围栏为所有帧资源和立即提交提供完成序列
    Microsoft::WRL::ComPtr<ID3D12Fence> _fence; // 所有 direct queue 提交的完成序列
    HANDLE _fenceEvent{nullptr}; // CPU 等待 fence 的事件对象
    std::uint64_t _nextFenceValue{1}; // 下一次提交分配的单调序列号

    // 每帧两个时间戳写入共享读回缓冲并延迟消费
    Microsoft::WRL::ComPtr<ID3D12QueryHeap> _timestampQueryHeap; // 每帧起止两个时间戳
    Microsoft::WRL::ComPtr<ID3D12Resource> _timestampReadback; // frame fence 后读取的时间戳结果
    std::uint64_t _timestampFrequency{0}; // direct queue 时间戳频率
    float _lastGpuFrameMilliseconds{0.0F}; // 最近完成帧的 GPU 时间
    float _lastGpuWaitMilliseconds{0.0F}; // 最近 BeginFrame 的 fence 等待时间

    // ImGui 字体槽位在后端整个初始化周期内保持稳定
    D3D12DescriptorAllocation _imguiFontDescriptor; // ImGui 后端保留的字体 SRV
    // LIFO 空闲表避免为固定容量描述符堆引入复杂分配器
    std::vector<std::uint32_t> _freeSrvIndices; // 未分配的 shader-visible 槽位
    std::uint32_t _rtvDescriptorSize{0}; // RTV 堆句柄步长
    std::uint32_t _srvDescriptorSize{0}; // SRV 堆句柄步长

    // frameIndex 始终与当前交换链缓冲一致
    std::uint32_t _frameIndex{0}; // 当前 back buffer 索引
    std::uint32_t _swapChainWidth{0}; // 已分配交换链宽度
    std::uint32_t _swapChainHeight{0}; // 已分配交换链高度
    int _drawableWidth{0}; // SDL 报告的最新 drawable 宽度
    int _drawableHeight{0}; // SDL 报告的最新 drawable 高度

    // frameOpen 防止重复 Reset 或重复 Present
    bool _vSyncEnabled{false}; // Present 是否等待垂直同步
    bool _tearingSupported{false}; // 无同步 Present 是否可使用 tearing flag
    bool _frameOpen{false}; // 命令列表是否处于录制周期
    bool _initialized{false}; // 设备到 frame resource 是否完整创建
    GraphicsDeviceCapabilities _deviceCapabilities{}; // 算法能力门禁使用的设备特性
    std::string _adapterName; // GUI 和 benchmark 输出的适配器名称
    std::string _versionString{"Direct3D 12 (feature level 12_0)"}; // 固定后端版本描述
};
} // namespace ParallelRoam::Render
