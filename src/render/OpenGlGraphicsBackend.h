#pragma once

#include "render/GraphicsBackend.h"

#include <SDL.h>

namespace ParallelRoam::Render
{
/// <summary>
/// 保留迁移前 OpenGL 行为的图形后端适配器
/// </summary>
class OpenGlGraphicsBackend final : public IGraphicsBackend
{
public:
    OpenGlGraphicsBackend() = default;
    ~OpenGlGraphicsBackend() override;

    OpenGlGraphicsBackend(const OpenGlGraphicsBackend&) = delete;
    OpenGlGraphicsBackend& operator=(const OpenGlGraphicsBackend&) = delete;

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

private:
    // SDL 窗口由 Window 持有，OpenGL context 由本类持有
    SDL_Window* _window{nullptr}; // SDL 窗口借用指针
    SDL_GLContext _context{nullptr}; // 本后端拥有的 OpenGL context
    // drawable 尺寸独立于 HiDPI 下的逻辑窗口尺寸
    int _drawableWidth{0}; // HiDPI 实际 framebuffer 宽度
    int _drawableHeight{0}; // HiDPI 实际 framebuffer 高度
    int _swapInterval{-1}; // SDL 当前确认的交换间隔
    GraphicsDeviceCapabilities _deviceCapabilities{}; // compute 能力门禁结果
    std::string _adapterName; // GL_RENDERER 字符串
    std::string _versionString; // GL_VERSION 字符串
};
} // namespace ParallelRoam::Render
