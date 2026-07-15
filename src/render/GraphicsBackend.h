#pragma once

#include <cstdint>
#include <memory>
#include <string>

struct SDL_Window;

namespace ParallelRoam::Gui
{
class ImGuiLayer;
}

namespace ParallelRoam::Render
{
enum class GraphicsApi
{
    OpenGl,
    Direct3D12,
};

/// <summary>
/// 应用主循环依赖的最小图形后端边界。
/// TerrainRenderer 和 GPU 算法的资源接口会在后续阶段继续迁移。
/// </summary>
class IGraphicsBackend
{
public:
    virtual ~IGraphicsBackend() = default;

    [[nodiscard]] virtual GraphicsApi Api() const = 0;
    [[nodiscard]] virtual const char* Name() const = 0;

    /// <summary>
    /// SDL 视频子系统初始化后、窗口创建前配置后端所需窗口属性。
    /// </summary>
    [[nodiscard]] virtual bool ConfigureWindow(std::string* errorMessage) = 0;
    [[nodiscard]] virtual std::uint32_t RequiredSdlWindowFlags() const = 0;

    [[nodiscard]] virtual bool Initialize(SDL_Window* window, std::string* errorMessage) = 0;
    [[nodiscard]] virtual bool InitializeImGui(Gui::ImGuiLayer& guiLayer, std::string* errorMessage) = 0;
    virtual void WaitForGpuIdle() = 0;
    virtual void Shutdown() = 0;

    virtual void BeginFrame() = 0;
    virtual void BeginImGuiFrame(Gui::ImGuiLayer& guiLayer) = 0;
    virtual void RenderImGui(Gui::ImGuiLayer& guiLayer) = 0;
    virtual void Present() = 0;
    virtual void RefreshDrawableSize() = 0;

    [[nodiscard]] virtual bool SetVSyncEnabled(bool enabled) = 0;
    [[nodiscard]] virtual bool VSyncEnabled() const = 0;
    [[nodiscard]] virtual int DrawableWidth() const = 0;
    [[nodiscard]] virtual int DrawableHeight() const = 0;
    [[nodiscard]] virtual bool UsesZeroToOneDepth() const = 0;
    [[nodiscard]] virtual const std::string& AdapterName() const = 0;
    [[nodiscard]] virtual const std::string& VersionString() const = 0;
    [[nodiscard]] virtual bool SupportsGpuRoamLike() const = 0;
    [[nodiscard]] virtual float LastGpuFrameMilliseconds() const = 0;
    [[nodiscard]] virtual float LastGpuWaitMilliseconds() const = 0;
    [[nodiscard]] virtual bool IsValid() const = 0;
};

[[nodiscard]] std::unique_ptr<IGraphicsBackend> CreateConfiguredGraphicsBackend();
[[nodiscard]] const char* ConfiguredGraphicsApiName();
} // namespace ParallelRoam::Render
