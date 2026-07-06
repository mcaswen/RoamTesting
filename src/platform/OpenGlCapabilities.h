#pragma once

#include <string>

namespace ParallelRoam::Platform
{
/// <summary>
/// 当前 OpenGL context 暴露的 GPU 计算与提交能力
/// </summary>
struct OpenGlGpuCapabilities
{
    bool HasActiveContext{false};
    int MajorVersion{0};
    int MinorVersion{0};
    std::string VersionString;
    std::string RendererString;
    bool SupportsOpenGl43{false};
    bool SupportsComputeShader{false};
    bool SupportsShaderStorageBufferObject{false};
    bool SupportsAtomicCounters{false};
    bool SupportsIndirectDraw{false};
    bool SupportsTimerQuery{false};

    [[nodiscard]] bool SupportsGpuRoamCompute() const;
    [[nodiscard]] std::string GpuRoamComputeUnavailableReason() const;
};

[[nodiscard]] OpenGlGpuCapabilities QueryOpenGlGpuCapabilities();
} // namespace ParallelRoam::Platform
