#pragma once

#include "render/GraphicsBackend.h"

#include <SDL.h>

namespace ParallelRoam::Render
{
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
    [[nodiscard]] float LastGpuFrameMilliseconds() const override;
    [[nodiscard]] float LastGpuWaitMilliseconds() const override;
    [[nodiscard]] bool IsValid() const override;

private:
    SDL_Window* _window{nullptr};
    SDL_GLContext _context{nullptr};
    int _drawableWidth{0};
    int _drawableHeight{0};
    int _swapInterval{-1};
    std::string _adapterName;
    std::string _versionString;
};
} // namespace ParallelRoam::Render
