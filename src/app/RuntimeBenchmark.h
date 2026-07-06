#pragma once

#include "algorithms/ITerrainLodAlgorithm.h"
#include "render/TerrainRenderer.h"

#include <glm/glm.hpp>

#include <filesystem>
#include <string>
#include <vector>

namespace ParallelRoam::App
{
/// <summary>
/// 运行时相机路径 benchmark 的单帧采样
/// </summary>
struct RuntimeBenchmarkSample
{
    // 路径内时间用于把每帧样本映射回 10 秒回放
    float TimeSeconds{0.0F};

    // 相机位置和 renderer stats 一起写入 CSV，便于复现实验点
    glm::vec3 CameraPosition{0.0F};

    // 帧耗时来自主循环 raw delta，不受相机移动 clamp 影响
    float FrameMilliseconds{0.0F};

    // TerrainRenderStats 已经是 UI、renderer 和算法层的共享口径
    Render::TerrainRenderStats Stats;
};

/// <summary>
/// 单个算法在同一条相机路径上的采样集合
/// </summary>
struct RuntimeBenchmarkAlgorithmResult
{
    // AlgorithmId 用于后续扩展 GPU 路径时保持稳定排序
    Algorithms::TerrainLodAlgorithmId AlgorithmId{Algorithms::TerrainLodAlgorithmId::ClassicCpuRoam};

    // AlgorithmName 写进 UI 和输出文件，避免报告依赖枚举值
    std::string AlgorithmName;

    // Samples 保留逐帧明细，汇总表只从这里二次聚合
    std::vector<RuntimeBenchmarkSample> Samples;
};

/// <summary>
/// benchmark 输出的汇总表和逐帧明细路径
/// </summary>
struct RuntimeBenchmarkReportPaths
{
    // MarkdownPath 是用户直接阅读的汇总表
    std::filesystem::path MarkdownPath;

    // CsvPath 保存逐帧样本，方便后续画图或做回归阈值
    std::filesystem::path CsvPath;
};

[[nodiscard]] std::string RuntimeBenchmarkAlgorithmDisplayName(Algorithms::TerrainLodAlgorithmId algorithmId);

[[nodiscard]] RuntimeBenchmarkReportPaths WriteRuntimeBenchmarkReport(
    const std::vector<RuntimeBenchmarkAlgorithmResult>& results);
} // namespace ParallelRoam::App
