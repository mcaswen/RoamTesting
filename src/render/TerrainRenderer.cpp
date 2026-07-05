#include "render/TerrainRenderer.h"

#include <glad/gl.h>
#include <stb_image.h>

#include <algorithm>
#include <cstddef>
#include <iostream>
#include <vector>

namespace ParallelRoam::Render
{
namespace
{
constexpr const char* VertexShaderSource = R"(
#version 410 core
layout (location = 0) in vec3 aPosition;
layout (location = 1) in vec3 aNormal;
layout (location = 2) in vec2 aTexCoord;
layout (location = 3) in float aHeight;

uniform mat4 uView;
uniform mat4 uProjection;

out vec3 vWorldPosition;
out vec3 vNormal;
out vec2 vTexCoord;
out float vHeight;

void main()
{
    vWorldPosition = aPosition;
    vNormal = aNormal;
    vTexCoord = aTexCoord;
    vHeight = aHeight;
    gl_Position = uProjection * uView * vec4(aPosition, 1.0);
}
)";

constexpr const char* FragmentShaderSource = R"(
#version 410 core
in vec3 vWorldPosition;
in vec3 vNormal;
in vec2 vTexCoord;
in float vHeight;

uniform sampler2D uTerrainTexture;
uniform vec3 uCameraPosition;
uniform vec3 uLightDirection;
uniform vec3 uLightColor;
uniform float uAmbientStrength;
uniform float uDiffuseStrength;
uniform float uSpecularStrength;

out vec4 FragColor;

void main()
{
    vec3 normal = normalize(vNormal);
    vec3 lightDir = normalize(-uLightDirection);
    vec3 viewDir = normalize(uCameraPosition - vWorldPosition);
    vec3 halfDir = normalize(lightDir + viewDir);

    vec3 textureColor = texture(uTerrainTexture, vTexCoord * 12.0).rgb;
    vec3 heightTint = mix(vec3(0.10, 0.32, 0.12), vec3(0.66, 0.62, 0.48), smoothstep(0.2, 0.92, vHeight));
    vec3 baseColor = mix(textureColor, heightTint, 0.35);

    float diffuse = max(dot(normal, lightDir), 0.0);
    float specular = pow(max(dot(normal, halfDir), 0.0), 32.0);
    vec3 lighting = baseColor * (uAmbientStrength + diffuse * uDiffuseStrength) * uLightColor;
    lighting += uLightColor * specular * uSpecularStrength;

    FragColor = vec4(lighting, 1.0);
}
)";

bool NeedsMeshRebuild(const TerrainRenderSettings& previous, const TerrainRenderSettings& next)
{
    return previous.TerrainSize != next.TerrainSize ||
           previous.HeightScale != next.HeightScale ||
           previous.UseClassicRoam != next.UseClassicRoam ||
           previous.RoamMaxDepth != next.RoamMaxDepth ||
           previous.RoamSplitThreshold != next.RoamSplitThreshold ||
           previous.RoamMergeThreshold != next.RoamMergeThreshold ||
           previous.RoamDistanceScale != next.RoamDistanceScale ||
           previous.RoamEnableCrackFix != next.RoamEnableCrackFix;
}

glm::vec3 NormalizeLightDirection(const glm::vec3& lightDirection)
{
    if (glm::dot(lightDirection, lightDirection) <= 0.000001F)
    {
        return glm::normalize(glm::vec3{-0.45F, -1.0F, -0.35F});
    }

    return glm::normalize(lightDirection);
}
} // 匿名命名空间

TerrainRenderer::~TerrainRenderer()
{
    Shutdown();
}

bool TerrainRenderer::Initialize(
    const std::filesystem::path& heightMapPath,
    const std::filesystem::path& texturePath,
    const TerrainRenderSettings& settings,
    std::string* errorMessage)
{
    _settings = settings;
    _heightMapPath = heightMapPath;
    _texturePath = texturePath;

    if (!_heightMap.LoadFromFile(heightMapPath, errorMessage))
    {
        return false;
    }

    if (!_shader.Load(VertexShaderSource, FragmentShaderSource, errorMessage))
    {
        return false;
    }

    if (!RebuildMesh(errorMessage))
    {
        return false;
    }

    if (!LoadTexture(texturePath, errorMessage))
    {
        return false;
    }

    _initialized = true;
    return true;
}

bool TerrainRenderer::ApplySettings(const TerrainRenderSettings& settings, std::string* errorMessage)
{
    const bool rebuildMesh = NeedsMeshRebuild(_settings, settings);
    _settings = settings;

    _meshDirty = _meshDirty || rebuildMesh;
    if (!_meshDirty)
    {
        return true;
    }

    return RebuildMesh(errorMessage);
}

bool TerrainRenderer::UpdateForCamera(const glm::vec3& cameraPosition, std::string* errorMessage)
{
    _lastCameraPosition = cameraPosition;

    // 规则网格不依赖相机，只有 UI 改变 mesh 参数时才重建
    if (!_settings.UseClassicRoam && !_meshDirty)
    {
        return true;
    }

    // Classic ROAM 是视点相关 LOD，阶段 2 先每帧重建保证行为直观
    if (_settings.UseClassicRoam)
    {
        return RebuildClassicRoam(cameraPosition, errorMessage);
    }

    return RebuildMesh(errorMessage);
}

void TerrainRenderer::Shutdown()
{
    if (_textureId != 0)
    {
        glDeleteTextures(1, &_textureId);
        _textureId = 0;
    }

    if (_indexBufferId != 0)
    {
        glDeleteBuffers(1, &_indexBufferId);
        _indexBufferId = 0;
    }

    if (_vertexBufferId != 0)
    {
        glDeleteBuffers(1, &_vertexBufferId);
        _vertexBufferId = 0;
    }

    if (_vertexArrayId != 0)
    {
        glDeleteVertexArrays(1, &_vertexArrayId);
        _vertexArrayId = 0;
    }

    _shader.Destroy();
    _initialized = false;
}

void TerrainRenderer::Render(const RenderContext& context)
{
    if (!_initialized || _meshData.Indices.empty())
    {
        return;
    }

    glViewport(0, 0, context.DrawableWidth, context.DrawableHeight);
    glEnable(GL_DEPTH_TEST);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, _textureId);

    _shader.Use();
    _shader.SetMat4("uView", context.View);
    _shader.SetMat4("uProjection", context.Projection);
    _shader.SetVec3("uCameraPosition", context.CameraPosition);
    _shader.SetVec3("uLightDirection", NormalizeLightDirection(_settings.LightDirection));
    _shader.SetVec3("uLightColor", _settings.LightColor);
    _shader.SetFloat("uAmbientStrength", _settings.AmbientStrength);
    _shader.SetFloat("uDiffuseStrength", _settings.DiffuseStrength);
    _shader.SetFloat("uSpecularStrength", _settings.SpecularStrength);
    _shader.SetInt("uTerrainTexture", 0);

    glBindVertexArray(_vertexArrayId);

    GLint previousPolygonMode[2] = {GL_FILL, GL_FILL};
    glGetIntegerv(GL_POLYGON_MODE, previousPolygonMode);

    if (_settings.Wireframe)
    {
        glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
    }

    glDrawElements(
        GL_TRIANGLES,
        static_cast<GLsizei>(_meshData.Indices.size()),
        GL_UNSIGNED_INT,
        nullptr);

    if (_settings.Wireframe)
    {
        glPolygonMode(GL_FRONT_AND_BACK, static_cast<GLenum>(previousPolygonMode[0]));
    }

    glBindVertexArray(0);
    glBindTexture(GL_TEXTURE_2D, 0);
}

TerrainRenderStats TerrainRenderer::Stats() const
{
    TerrainRenderStats stats{};
    stats.HeightMapWidth = _heightMap.Width();
    stats.HeightMapHeight = _heightMap.Height();
    stats.VertexCount = _meshData.Vertices.size();
    stats.TriangleCount = _meshData.Indices.size() / 3U;
    stats.DrawCallCount = _initialized ? 1 : 0;
    stats.TerrainSize = _settings.TerrainSize;
    stats.HeightScale = _settings.HeightScale;
    stats.UseClassicRoam = _settings.UseClassicRoam;
    stats.RoamNodeCount = _classicRoamStats.NodeCount;
    stats.RoamSplitCount = _classicRoamStats.SplitCount;
    stats.RoamForcedSplitCount = _classicRoamStats.ForcedSplitCount;
    stats.RoamMergeCount = _classicRoamStats.MergeCount;
    stats.RoamCrackRiskCount = _classicRoamStats.CrackRiskCount;
    stats.RoamConstraintPassCount = _classicRoamStats.ConstraintPassCount;
    stats.RoamMaxDepthReached = _classicRoamStats.MaxDepthReached;
    return stats;
}

const std::filesystem::path& TerrainRenderer::HeightMapPath() const
{
    return _heightMapPath;
}

const std::filesystem::path& TerrainRenderer::TexturePath() const
{
    return _texturePath;
}

bool TerrainRenderer::RebuildMesh(std::string* errorMessage)
{
    if (_settings.UseClassicRoam)
    {
        return RebuildClassicRoam(_lastCameraPosition, errorMessage);
    }

    return RebuildRegularGrid(errorMessage);
}

bool TerrainRenderer::RebuildRegularGrid(std::string* errorMessage)
{
    // 规则网格会重置 ROAM 统计，避免 UI 显示上一次算法结果
    _meshData = Terrain::TerrainMeshBuilder::Build(_heightMap, _settings.TerrainSize, _settings.HeightScale);
    _classicRoamStats = {};
    if (_meshData.Vertices.empty() || _meshData.Indices.empty())
    {
        if (errorMessage != nullptr)
        {
            *errorMessage = "Terrain mesh build failed: invalid height map or grid size";
        }
        return false;
    }

    _meshDirty = false;
    return UploadMesh(errorMessage);
}

bool TerrainRenderer::RebuildClassicRoam(const glm::vec3& cameraPosition, std::string* errorMessage)
{
    Algorithms::ClassicRoam::ClassicRoamSettings roamSettings{};
    roamSettings.MaxDepth = _settings.RoamMaxDepth;
    roamSettings.SplitThreshold = _settings.RoamSplitThreshold;
    roamSettings.MergeThreshold = _settings.RoamMergeThreshold;
    roamSettings.DistanceScale = _settings.RoamDistanceScale;
    roamSettings.EnableCrackFix = _settings.RoamEnableCrackFix;

    // Classic ROAM 输出 TerrainMeshData，后续 renderer 不需要关心算法细节
    _meshData = _classicRoamBuilder.Build(
        _heightMap,
        _settings.TerrainSize,
        _settings.HeightScale,
        cameraPosition,
        roamSettings);
    _classicRoamStats = _classicRoamBuilder.Stats();

    if (_meshData.Vertices.empty() || _meshData.Indices.empty())
    {
        if (errorMessage != nullptr)
        {
            *errorMessage = "Classic ROAM mesh build failed";
        }
        return false;
    }

    _meshDirty = false;
    return UploadMesh(errorMessage);
}

bool TerrainRenderer::UploadMesh(std::string* errorMessage)
{
    // buffer 对象只创建一次，后续 mesh 变化只更新数据内容
    if (_vertexArrayId == 0)
    {
        glGenVertexArrays(1, &_vertexArrayId);
    }

    if (_vertexBufferId == 0)
    {
        glGenBuffers(1, &_vertexBufferId);
    }

    if (_indexBufferId == 0)
    {
        glGenBuffers(1, &_indexBufferId);
    }

    if (_vertexArrayId == 0 || _vertexBufferId == 0 || _indexBufferId == 0)
    {
        if (errorMessage != nullptr)
        {
            *errorMessage = "OpenGL buffer allocation failed";
        }
        return false;
    }

    glBindVertexArray(_vertexArrayId);
    glBindBuffer(GL_ARRAY_BUFFER, _vertexBufferId);
    glBufferData(
        GL_ARRAY_BUFFER,
        static_cast<GLsizeiptr>(_meshData.Vertices.size() * sizeof(Terrain::TerrainMeshVertex)),
        _meshData.Vertices.data(),
        GL_STATIC_DRAW);

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, _indexBufferId);
    glBufferData(
        GL_ELEMENT_ARRAY_BUFFER,
        static_cast<GLsizeiptr>(_meshData.Indices.size() * sizeof(std::uint32_t)),
        _meshData.Indices.data(),
        GL_STATIC_DRAW);

    glEnableVertexAttribArray(0);
    glVertexAttribPointer(
        0,
        3,
        GL_FLOAT,
        GL_FALSE,
        static_cast<GLsizei>(sizeof(Terrain::TerrainMeshVertex)),
        reinterpret_cast<const void*>(offsetof(Terrain::TerrainMeshVertex, Position)));

    glEnableVertexAttribArray(1);
    glVertexAttribPointer(
        1,
        3,
        GL_FLOAT,
        GL_FALSE,
        static_cast<GLsizei>(sizeof(Terrain::TerrainMeshVertex)),
        reinterpret_cast<const void*>(offsetof(Terrain::TerrainMeshVertex, Normal)));

    glEnableVertexAttribArray(2);
    glVertexAttribPointer(
        2,
        2,
        GL_FLOAT,
        GL_FALSE,
        static_cast<GLsizei>(sizeof(Terrain::TerrainMeshVertex)),
        reinterpret_cast<const void*>(offsetof(Terrain::TerrainMeshVertex, TexCoord)));

    glEnableVertexAttribArray(3);
    glVertexAttribPointer(
        3,
        1,
        GL_FLOAT,
        GL_FALSE,
        static_cast<GLsizei>(sizeof(Terrain::TerrainMeshVertex)),
        reinterpret_cast<const void*>(offsetof(Terrain::TerrainMeshVertex, Height)));

    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    return true;
}

bool TerrainRenderer::LoadTexture(const std::filesystem::path& texturePath, std::string* errorMessage)
{
    int width = 0;
    int height = 0;
    int channelCount = 0;
    unsigned char* pixels = stbi_load(texturePath.string().c_str(), &width, &height, &channelCount, 4);

    if (pixels == nullptr)
    {
        if (errorMessage != nullptr)
        {
            *errorMessage = "Terrain texture load failed: " + texturePath.string() + "\n" + stbi_failure_reason();
        }
        return false;
    }

    if (_textureId == 0)
    {
        glGenTextures(1, &_textureId);
    }

    glBindTexture(GL_TEXTURE_2D, _textureId);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexImage2D(
        GL_TEXTURE_2D,
        0,
        GL_RGBA8,
        width,
        height,
        0,
        GL_RGBA,
        GL_UNSIGNED_BYTE,
        pixels);
    glGenerateMipmap(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, 0);

    stbi_image_free(pixels);
    return true;
}
} // 命名空间 ParallelRoam::Render
