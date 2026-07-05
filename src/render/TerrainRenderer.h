#pragma once

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
    bool UploadMesh(std::string* errorMessage);
    bool LoadTexture(const std::filesystem::path& texturePath, std::string* errorMessage);

    Shader _shader;
    Terrain::HeightMap _heightMap;
    Terrain::TerrainMeshData _meshData;
    TerrainRenderSettings _settings;
    std::filesystem::path _heightMapPath;
    std::filesystem::path _texturePath;
    unsigned int _vertexArrayId{0};
    unsigned int _vertexBufferId{0};
    unsigned int _indexBufferId{0};
    unsigned int _textureId{0};
    bool _initialized{false};
};
} // 命名空间 ParallelRoam::Render
