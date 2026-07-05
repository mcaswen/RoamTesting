#include "gui/ImGuiLayer.h"

#include <imgui.h>
#include <imgui_impl_opengl3.h>
#include <imgui_impl_sdl2.h>

#include <algorithm>
#include <array>
#include <cstdio>
#include <filesystem>

namespace ParallelRoam::Gui
{
namespace
{
// GUI 层只编辑配置和展示统计
// 它不直接触碰 renderer buffer 或算法节点
constexpr float PanelMinWidth = 330.0F;
constexpr float PanelMaxWidth = 390.0F;

// DrawDebugOverlay 通过返回 changed 通知 Application 应用新设置
// 这样 UI 控件和渲染/算法边界保持单向数据流
constexpr float MetricValueOffset = 132.0F;

void ApplyEditorStyle()
{
    // 样式在初始化时集中设置
    // 避免每帧重复写 ImGuiStyle
    // 也让截图和录屏的 UI 外观稳定
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowPadding = ImVec2{16.0F, 14.0F};
    style.FramePadding = ImVec2{10.0F, 6.0F};
    style.ItemSpacing = ImVec2{10.0F, 9.0F};
    style.ItemInnerSpacing = ImVec2{8.0F, 6.0F};
    style.ScrollbarSize = 11.0F;
    style.WindowBorderSize = 0.0F;
    style.ChildBorderSize = 0.0F;
    style.PopupBorderSize = 1.0F;
    style.FrameBorderSize = 0.0F;
    style.WindowRounding = 0.0F;
    style.ChildRounding = 0.0F;
    style.FrameRounding = 4.0F;
    style.GrabRounding = 4.0F;
    style.TabRounding = 4.0F;

    ImVec4* colors = style.Colors;
    colors[ImGuiCol_Text] = ImVec4{0.88F, 0.91F, 0.90F, 1.0F};
    colors[ImGuiCol_TextDisabled] = ImVec4{0.47F, 0.52F, 0.52F, 1.0F};
    colors[ImGuiCol_WindowBg] = ImVec4{0.07F, 0.085F, 0.09F, 0.98F};
    colors[ImGuiCol_ChildBg] = ImVec4{0.085F, 0.105F, 0.11F, 1.0F};
    colors[ImGuiCol_PopupBg] = ImVec4{0.07F, 0.085F, 0.09F, 1.0F};
    colors[ImGuiCol_Border] = ImVec4{0.20F, 0.25F, 0.25F, 1.0F};
    colors[ImGuiCol_FrameBg] = ImVec4{0.12F, 0.15F, 0.155F, 1.0F};
    colors[ImGuiCol_FrameBgHovered] = ImVec4{0.16F, 0.20F, 0.205F, 1.0F};
    colors[ImGuiCol_FrameBgActive] = ImVec4{0.18F, 0.25F, 0.25F, 1.0F};
    colors[ImGuiCol_TitleBg] = ImVec4{0.07F, 0.085F, 0.09F, 1.0F};
    colors[ImGuiCol_TitleBgActive] = ImVec4{0.07F, 0.085F, 0.09F, 1.0F};
    colors[ImGuiCol_Header] = ImVec4{0.10F, 0.14F, 0.145F, 1.0F};
    colors[ImGuiCol_HeaderHovered] = ImVec4{0.16F, 0.23F, 0.23F, 1.0F};
    colors[ImGuiCol_HeaderActive] = ImVec4{0.13F, 0.18F, 0.18F, 1.0F};
    colors[ImGuiCol_Button] = ImVec4{0.12F, 0.18F, 0.18F, 1.0F};
    colors[ImGuiCol_ButtonHovered] = ImVec4{0.18F, 0.27F, 0.27F, 1.0F};
    colors[ImGuiCol_ButtonActive] = ImVec4{0.11F, 0.34F, 0.31F, 1.0F};
    colors[ImGuiCol_CheckMark] = ImVec4{0.24F, 0.72F, 0.61F, 1.0F};
    colors[ImGuiCol_SliderGrab] = ImVec4{0.24F, 0.72F, 0.61F, 1.0F};
    colors[ImGuiCol_SliderGrabActive] = ImVec4{0.34F, 0.86F, 0.72F, 1.0F};
    colors[ImGuiCol_Separator] = ImVec4{0.18F, 0.24F, 0.24F, 1.0F};
    colors[ImGuiCol_ResizeGrip] = ImVec4{0.0F, 0.0F, 0.0F, 0.0F};
    colors[ImGuiCol_ScrollbarBg] = ImVec4{0.06F, 0.075F, 0.08F, 1.0F};
    colors[ImGuiCol_ScrollbarGrab] = ImVec4{0.18F, 0.24F, 0.24F, 1.0F};
    colors[ImGuiCol_ScrollbarGrabHovered] = ImVec4{0.24F, 0.32F, 0.32F, 1.0F};
}

void LoadChineseFont()
{
    ImGuiIO& io = ImGui::GetIO();
    // 项目可能只携带其中一个中文字体文件
    // 按常见命名顺序探测
    // 找不到时回退默认字体让程序仍可启动
    const std::array<std::filesystem::path, 8> fontCandidates{
        std::filesystem::path{"assets/fonts/NotoSansSC-Regular.otf"},
        std::filesystem::path{"assets/fonts/NotoSansSC-Regular.ttf"},
        std::filesystem::path{"assets/fonts/SourceHanSansSC-Regular.otf"},
        std::filesystem::path{"assets/fonts/SourceHanSansSC-Regular.ttf"},
        std::filesystem::path{"assets/fonts/思源黑体-Regular.otf"},
        std::filesystem::path{"assets/fonts/思源黑体-Regular.ttf"},
        std::filesystem::path{"assets/fonts/AlibabaPuHuiTi-3-55-Regular.ttf"},
        std::filesystem::path{"assets/fonts/AlibabaPuHuiTi-Regular.ttf"},
    };

    // 中文字体必须显式载入 CJK glyph range，默认 ImGui 字体不覆盖中文
    for (const std::filesystem::path& fontPath : fontCandidates)
    {
        if (!std::filesystem::exists(fontPath))
        {
            continue;
        }

        ImFontConfig fontConfig{};
        // CJK 字体边缘在小字号下容易糊
        // 适度 oversample 提升调试面板可读性
        fontConfig.OversampleH = 3;
        fontConfig.OversampleV = 2;
        fontConfig.PixelSnapH = false;

        if (io.Fonts->AddFontFromFileTTF(
                fontPath.string().c_str(),
                17.0F,
                &fontConfig,
                io.Fonts->GetGlyphRangesChineseFull()) != nullptr)
        {
            return;
        }
    }

    io.Fonts->AddFontDefault();
}

void DrawSectionHeader(const char* label)
{
    ImGui::Spacing();
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4{0.24F, 0.72F, 0.61F, 1.0F});
    ImGui::TextUnformatted(label);
    ImGui::PopStyleColor();
    ImGui::Separator();
}

void DrawMetricRow(const char* label, const char* value)
{
    // metric 使用固定 value 偏移
    // 长 label 不会挤压右侧数值列
    ImGui::TextDisabled("%s", label);
    ImGui::SameLine(MetricValueOffset);
    ImGui::TextUnformatted(value);
}

void DrawMetricInt(const char* label, int value)
{
    char buffer[64]{};
    std::snprintf(buffer, sizeof(buffer), "%d", value);
    DrawMetricRow(label, buffer);
}

void DrawMetricSize(const char* label, std::size_t value)
{
    char buffer[64]{};
    std::snprintf(buffer, sizeof(buffer), "%zu", value);
    DrawMetricRow(label, buffer);
}

void DrawMetricFloat(const char* label, float value, const char* format)
{
    char buffer[64]{};
    std::snprintf(buffer, sizeof(buffer), format, static_cast<double>(value));
    DrawMetricRow(label, buffer);
}

void DrawDebugColorLegend()
{
    // legend 与 TerrainMeshVertex 的 debug color 语义对应
    // 用户打开 LOD 着色后能直接解释当前颜色
    const std::array<std::pair<const char*, ImVec4>, 3> legendItems{
        std::pair<const char*, ImVec4>{"原始", ImVec4{0.28F, 0.34F, 0.30F, 1.0F}},
        std::pair<const char*, ImVec4>{"细分", ImVec4{0.08F, 0.72F, 0.62F, 1.0F}},
        std::pair<const char*, ImVec4>{"重建", ImVec4{1.0F, 0.58F, 0.12F, 1.0F}},
    };

    for (const auto& [label, color] : legendItems)
    {
        ImGui::ColorButton(label, color, ImGuiColorEditFlags_NoTooltip | ImGuiColorEditFlags_NoDragDrop, ImVec2{14.0F, 14.0F});
        ImGui::SameLine();
        ImGui::TextUnformatted(label);
        ImGui::SameLine();
    }

    ImGui::NewLine();
}
} // 匿名命名空间

bool ImGuiLayer::Initialize(SDL_Window* window, SDL_GLContext glContext, const char* glslVersion)
{
    // ImGui context 必须先创建，backend 初始化会访问当前 context
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();

    ImGuiIO& io = ImGui::GetIO();
    LoadChineseFont();
    // 键盘导航先打开，后续增加 GUI 控件时不用再改初始化路径
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

    ImGui::StyleColorsDark();
    ApplyEditorStyle();

    // SDL2 backend 负责事件和输入状态桥接
    // 失败时必须销毁 context
    // 否则后续重新初始化会留下 ImGui 全局状态
    if (!ImGui_ImplSDL2_InitForOpenGL(window, glContext))
    {
        ImGui::DestroyContext();
        return false;
    }

    // OpenGL3 backend 负责生成 GUI draw call
    // SDL backend 已经成功时
    // OpenGL backend 失败必须按相反顺序清理
    if (!ImGui_ImplOpenGL3_Init(glslVersion))
    {
        ImGui_ImplSDL2_Shutdown();
        ImGui::DestroyContext();
        return false;
    }

    _initialized = true;
    return true;
}

void ImGuiLayer::Shutdown()
{
    // Shutdown 可能被 Application 析构重复触发，因此需要状态保护
    if (!_initialized)
    {
        return;
    }

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();
    _initialized = false;
}

void ImGuiLayer::ProcessEvent(const SDL_Event& event)
{
    ImGui_ImplSDL2_ProcessEvent(&event);
}

void ImGuiLayer::BeginFrame()
{
    // backend new frame 必须早于 ImGui::NewFrame
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplSDL2_NewFrame();
    ImGui::NewFrame();
}

bool ImGuiLayer::DrawDebugOverlay(const DebugOverlayData& data, TerrainPanelState& terrainState)
{
    bool changed = false;
    const ImGuiIO& io = ImGui::GetIO();
    // 面板宽度跟随窗口
    // 同时限制最小和最大值
    // 避免宽屏上占用过多 terrain 视野
    const float panelWidth = std::clamp(io.DisplaySize.x * 0.25F, PanelMinWidth, PanelMaxWidth);
    const float panelX = std::max(0.0F, io.DisplaySize.x - panelWidth);

    // 右侧固定 inspector 面板避免遮挡左侧视野，也不随鼠标误拖动
    ImGui::SetNextWindowPos(ImVec2{panelX, 0.0F}, ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2{panelWidth, io.DisplaySize.y}, ImGuiCond_Always);

    constexpr ImGuiWindowFlags PanelFlags =
        // inspector 固定为工具面板
        // 不保存 ImGui ini 状态
        // 每次启动都出现在相同位置
        ImGuiWindowFlags_NoTitleBar |
        ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoSavedSettings |
        ImGuiWindowFlags_NoBringToFrontOnFocus;

    ImGui::Begin("Parallel ROAM Inspector", nullptr, PanelFlags);
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4{0.92F, 0.96F, 0.94F, 1.0F});
    ImGui::TextUnformatted("Parallel ROAM");
    ImGui::PopStyleColor();
    ImGui::TextDisabled("地形基线与运行诊断");

    DrawSectionHeader("运行状态");
    DrawMetricFloat("FPS", data.FramesPerSecond, "%.1f");
    DrawMetricFloat("Frame ms", data.FrameTimeMilliseconds, "%.2f");
    DrawMetricInt("Draw Call", data.DrawCallCount);
    DrawMetricInt("窗口宽度", data.WindowWidth);
    DrawMetricInt("窗口高度", data.WindowHeight);

    char cameraBuffer[96]{};
    std::snprintf(
        cameraBuffer,
        sizeof(cameraBuffer),
        "%.1f, %.1f, %.1f",
        static_cast<double>(data.CameraPosition.x),
        static_cast<double>(data.CameraPosition.y),
        static_cast<double>(data.CameraPosition.z));
    DrawMetricRow("相机位置", cameraBuffer);
    DrawMetricFloat("Yaw", data.CameraYawDegrees, "%.1f");
    DrawMetricFloat("Pitch", data.CameraPitchDegrees, "%.1f");

    DrawSectionHeader("地形");
    DrawMetricInt("高度图宽", data.HeightMapWidth);
    DrawMetricInt("高度图高", data.HeightMapHeight);
    DrawMetricSize("顶点数", data.VertexCount);
    DrawMetricSize("三角形数", data.TriangleCount);
    DrawMetricRow("模式", data.UseClassicRoam ? "Classic ROAM" : "规则网格");
    changed |= ImGui::Checkbox("线框模式", &terrainState.Wireframe);
    changed |= ImGui::Checkbox("Classic ROAM", &terrainState.UseClassicRoam);
    const char* debugColorModes[] = {"关闭", "LOD 状态"};
    changed |= ImGui::Combo("调试着色", &terrainState.DebugColorMode, debugColorModes, 2);
    changed |= ImGui::SliderFloat("着色强度", &terrainState.DebugOverlayStrength, 0.0F, 1.0F, "%.2f");
    if (terrainState.DebugColorMode == 1)
    {
        DrawDebugColorLegend();
    }
    changed |= ImGui::SliderFloat("地形尺寸", &terrainState.TerrainSize, 6.0F, 80.0F, "%.1f");
    changed |= ImGui::SliderFloat("高度缩放", &terrainState.HeightScale, 0.0F, 12.0F, "%.2f");

    DrawSectionHeader("ROAM");
    DrawMetricSize("节点数", data.RoamNodeCount);
    DrawMetricSize("原始三角", data.RoamOriginalTriangleCount);
    DrawMetricSize("细分三角", data.RoamSubdividedTriangleCount);
    DrawMetricSize("重建三角", data.RoamRebuiltTriangleCount);
    DrawMetricSize("活跃 Split", data.RoamActiveSplitCount);
    DrawMetricSize("本帧 Split", data.RoamSplitCount);
    DrawMetricSize("强制 Split", data.RoamForcedSplitCount);
    DrawMetricSize("Merge 数", data.RoamMergeCount);
    DrawMetricSize("约束传播", data.RoamConstraintPassCount);
    DrawMetricSize("队列峰值", data.RoamCandidatePeakCount);
    DrawMetricSize("预算拒绝", data.RoamRejectedSplitCount);
    DrawMetricSize("Merge 拒绝", data.RoamRejectedMergeCount);
    DrawMetricSize("T-junction", data.RoamTjunctionCount);
    DrawMetricSize("邻接错误", data.RoamInvalidNeighborCount);
    DrawMetricSize("拓扑错误", data.RoamInvalidTopologyCount);
    DrawMetricFloat("ROAM ms", data.RoamUpdateMilliseconds, "%.2f");
    DrawMetricFloat("Split ms", data.RoamSplitMilliseconds, "%.2f");
    DrawMetricFloat("Merge ms", data.RoamMergeMilliseconds, "%.2f");
    DrawMetricFloat("Emit ms", data.RoamEmitMilliseconds, "%.2f");
    DrawMetricFloat("Validate ms", data.RoamValidateMilliseconds, "%.2f");
    DrawMetricInt("实际深度", data.RoamMaxDepthReached);
    changed |= ImGui::Checkbox("局部约束", &terrainState.RoamEnableLocalConstraints);
    changed |= ImGui::Checkbox("拓扑验证", &terrainState.RoamEnableTopologyValidation);
    changed |= ImGui::SliderInt("最大深度", &terrainState.RoamMaxDepth, 1, 16);
    // 预算调节用于快速确认卡顿来自 split 数量还是拓扑验证
    changed |= ImGui::SliderInt("Split 预算", &terrainState.RoamSplitBudget, 256, 32768);
    changed |= ImGui::SliderFloat("Split 阈值", &terrainState.RoamSplitThreshold, 0.005F, 1.0F, "%.3f");
    changed |= ImGui::SliderFloat("Merge 阈值", &terrainState.RoamMergeThreshold, 0.001F, 1.0F, "%.3f");
    changed |= ImGui::SliderFloat("距离权重", &terrainState.RoamDistanceScale, 1.0F, 80.0F, "%.1f");
    // merge 阈值不能超过 split 阈值
    // UI 层先限制一次
    // 算法层仍会再次做防御性 clamp
    terrainState.RoamMergeThreshold = std::min(terrainState.RoamMergeThreshold, terrainState.RoamSplitThreshold);

    DrawSectionHeader("光照");
    changed |= ImGui::SliderFloat3("方向", &terrainState.LightDirection.x, -1.0F, 1.0F, "%.2f");
    changed |= ImGui::ColorEdit3("颜色", &terrainState.LightColor.x, ImGuiColorEditFlags_NoInputs);
    changed |= ImGui::SliderFloat("环境光", &terrainState.AmbientStrength, 0.0F, 1.0F, "%.2f");
    changed |= ImGui::SliderFloat("漫反射", &terrainState.DiffuseStrength, 0.0F, 2.0F, "%.2f");
    changed |= ImGui::SliderFloat("高光", &terrainState.SpecularStrength, 0.0F, 1.0F, "%.2f");
    ImGui::End();

    return changed;
}

void ImGuiLayer::EndFrame()
{
    // ImGui::Render 只生成 draw data，真正绘制由 OpenGL backend 完成
    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
}
} // 命名空间 ParallelRoam::Gui
