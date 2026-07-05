#pragma once

#include "algorithms/classic_roam/ClassicRoamMeshBuilder.h"
#include "render/Shader.h"
#include "terrain/HeightMap.h"
#include "terrain/TerrainMeshBuilder.h"

#include <filesystem>
#include <string>

namespace ParallelRoam::Render
{
/// <summary>
/// 单帧渲染上下文，承载相机矩阵和当前 drawable 尺寸
/// </summary>
struct RenderContext
{
    glm::mat4 View{1.0F};
    glm::mat4 Projection{1.0F};
    glm::vec3 CameraPosition{0.0F};
    int DrawableWidth{1};
    int DrawableHeight{1};
};

struct TerrainRenderSettings
{
    float TerrainSize{30.0F};
    float HeightScale{4.0F};
    bool Wireframe{false};
    bool UseClassicRoam{true};
    int RoamMaxDepth{8};

    // SplitThreshold 是进入细分的高水位阈值
    float RoamSplitThreshold{0.16F};

    // MergeThreshold 是回落粗网格的低水位阈值
    float RoamMergeThreshold{0.08F};
    float RoamDistanceScale{24.0F};

    // 裂缝修复开关用于对照 forced split 前后的网格差异
    bool RoamEnableCrackFix{true};
    glm::vec3 LightDirection{-0.45F, -1.0F, -0.35F};
    glm::vec3 LightColor{1.0F, 0.96F, 0.88F};
    float AmbientStrength{0.28F};
    float DiffuseStrength{0.85F};
    float SpecularStrength{0.18F};
};

struct TerrainRenderStats
{
    int HeightMapWidth{0};
    int HeightMapHeight{0};
    std::size_t VertexCount{0};
    std::size_t TriangleCount{0};
    int DrawCallCount{0};
    float TerrainSize{0.0F};
    float HeightScale{0.0F};
    bool UseClassicRoam{false};
    std::size_t RoamNodeCount{0};

    // 常规 split 来自误差阈值和相机距离
    std::size_t RoamSplitCount{0};

    // 强制 split 来自 baseNeighbor diamond 传播
    std::size_t RoamForcedSplitCount{0};

    // Merge 统计用于观察 hysteresis 是否稳定
    std::size_t RoamMergeCount{0};

    // CrackRisk 表示最大深度处仍无法继续修复的边界风险
    std::size_t RoamCrackRiskCount{0};

    // ConstraintPass 表示 baseNeighbor 约束传播了多少次
    std::size_t RoamConstraintPassCount{0};
    int RoamMaxDepthReached{0};
};

/// <summary>
/// 负责上传和绘制规则网格高度图地形
/// </summary>
class TerrainRenderer
{
public:
    TerrainRenderer() = default;
    ~TerrainRenderer();

    TerrainRenderer(const TerrainRenderer&) = delete;
    TerrainRenderer& operator=(const TerrainRenderer&) = delete;

    /// <summary>
    /// 加载高度图、地表纹理并创建 OpenGL 资源
    /// </summary>
    /// <param name="heightMapPath">高度图路径。</param>
    /// <param name="texturePath">地表纹理路径。</param>
    /// <param name="settings">初始渲染设置。</param>
    /// <param name="errorMessage">失败时写入错误信息。</param>
    bool Initialize(
        const std::filesystem::path& heightMapPath,
        const std::filesystem::path& texturePath,
        const TerrainRenderSettings& settings,
        std::string* errorMessage);

    /// <summary>
    /// 应用 UI 修改后的渲染设置，必要时重建 mesh
    /// </summary>
    /// <param name="settings">新的渲染设置。</param>
    /// <param name="errorMessage">失败时写入错误信息。</param>
    bool ApplySettings(const TerrainRenderSettings& settings, std::string* errorMessage);

    /// <summary>
    /// 根据相机位置更新 Classic ROAM mesh
    /// </summary>
    /// <param name="cameraPosition">相机世界坐标。</param>
    /// <param name="errorMessage">失败时写入错误信息。</param>
    bool UpdateForCamera(const glm::vec3& cameraPosition, std::string* errorMessage);

    /// <summary>
    /// 释放 OpenGL buffer、texture 和 shader program
    /// </summary>
    void Shutdown();

    /// <summary>
    /// 绘制当前 terrain mesh
    /// </summary>
    /// <param name="context">当前帧渲染上下文。</param>
    void Render(const RenderContext& context);

    [[nodiscard]] TerrainRenderStats Stats() const;
    [[nodiscard]] const std::filesystem::path& HeightMapPath() const;
    [[nodiscard]] const std::filesystem::path& TexturePath() const;

private:
    bool RebuildMesh(std::string* errorMessage);

    // 阶段 1 baseline 路径，便于和 ROAM 视觉对照
    bool RebuildRegularGrid(std::string* errorMessage);

    // 阶段 2 Classic ROAM 路径，会随相机位置动态更新
    bool RebuildClassicRoam(const glm::vec3& cameraPosition, std::string* errorMessage);
    bool UploadMesh(std::string* errorMessage);
    bool LoadTexture(const std::filesystem::path& texturePath, std::string* errorMessage);

    Shader _shader;
    Terrain::HeightMap _heightMap;
    Terrain::TerrainMeshData _meshData;
    Algorithms::ClassicRoam::ClassicRoamMeshBuilder _classicRoamBuilder;
    Algorithms::ClassicRoam::ClassicRoamStats _classicRoamStats;
    TerrainRenderSettings _settings;
    std::filesystem::path _heightMapPath;
    std::filesystem::path _texturePath;
    glm::vec3 _lastCameraPosition{0.0F};
    unsigned int _vertexArrayId{0};
    unsigned int _vertexBufferId{0};
    unsigned int _indexBufferId{0};
    unsigned int _textureId{0};
    bool _initialized{false};
    bool _meshDirty{true};
};
} // 命名空间 ParallelRoam::Render
