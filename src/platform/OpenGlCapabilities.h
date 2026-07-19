#pragma once

#include <string>

namespace ParallelRoam::Platform
{
/// <summary>
/// 当前 OpenGL context 暴露的 GPU 计算与提交能力
/// </summary>
struct OpenGlGpuCapabilities
{
    bool HasActiveContext{false}; // GLAD 函数和 GL_VERSION 均可读取
    int MajorVersion{0}; // 当前 context 核心版本主号
    int MinorVersion{0}; // 当前 context 核心版本次号
    std::string VersionString; // 驱动报告的完整 GL_VERSION
    std::string RendererString; // 驱动报告的 GPU renderer
    bool SupportsOpenGl43{false}; // 核心版本或 GLAD 版本标志满足 4.3
    bool SupportsComputeShader{false}; // compute shader 核心或扩展入口可用
    bool SupportsShaderStorageBufferObject{false}; // SSBO 核心或扩展入口可用
    bool SupportsAtomicCounters{false}; // 候选和压缩计数所需原子能力
    bool SupportsIndirectDraw{false}; // GPU 生成 draw command 后可直接提交
    bool SupportsTimerQuery{false}; // GPU compute benchmark 可获得非阻塞计时

    [[nodiscard]] bool SupportsGpuRoamCompute() const;
    [[nodiscard]] std::string GpuRoamComputeUnavailableReason() const;
};

[[nodiscard]] OpenGlGpuCapabilities QueryOpenGlGpuCapabilities();
} // namespace ParallelRoam::Platform
