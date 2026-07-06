#include "render/TerrainRenderer.h"

#include "algorithms/classic_roam/ClassicRoamTerrainLodAlgorithm.h"
#include "algorithms/data_oriented_roam/DataOrientedRoamTerrainLodAlgorithm.h"

#include <glad/gl.h>
#include <stb_image.h>

#include <algorithm>
#include <cstddef>
#include <iostream>
#include <memory>
#include <utility>
#include <vector>

namespace ParallelRoam::Render
{
namespace
{
// TerrainRenderer 只把算法输出转换成 OpenGL draw state
// LOD 决策留在 ITerrainLodAlgorithm 边界内
// 相机至少移动这个距离才触发 terrain LOD rebuild
constexpr float MinRoamRebuildDistance = 0.30F;

// meshDirty 只代表 CPU mesh 或 GPU buffer 内容需要重建
// shader uniform 变化不应该触发 mesh rebuild
// 地形越大，rebuild 位移阈值也按比例放大
constexpr float RoamRebuildTerrainScale = 0.01F;

constexpr const char* VertexShaderSource = R"(
#version 410 core
layout (location = 0) in vec3 aPosition;
layout (location = 1) in vec3 aNormal;
layout (location = 2) in vec2 aTexCoord;
layout (location = 3) in float aHeight;
layout (location = 4) in vec3 aDebugColor;
layout (location = 5) in float aDebugHighlight;

uniform mat4 uView;
uniform mat4 uProjection;

out vec3 vWorldPosition;
out vec3 vNormal;
out vec2 vTexCoord;
out float vHeight;
out vec3 vDebugColor;
out float vDebugHighlight;

void main()
{
    vWorldPosition = aPosition;
    vNormal = aNormal;
    vTexCoord = aTexCoord;
    vHeight = aHeight;
    vDebugColor = aDebugColor;
    vDebugHighlight = aDebugHighlight;
    gl_Position = uProjection * uView * vec4(aPosition, 1.0);
}
)";

constexpr const char* FragmentShaderSource = R"(
#version 410 core
in vec3 vWorldPosition;
in vec3 vNormal;
in vec2 vTexCoord;
in float vHeight;
in vec3 vDebugColor;
in float vDebugHighlight;

uniform sampler2D uTerrainTexture;
uniform vec3 uCameraPosition;
uniform vec3 uLightDirection;
uniform vec3 uLightColor;
uniform float uAmbientStrength;
uniform float uDiffuseStrength;
uniform float uSpecularStrength;
uniform int uDebugColorMode;
uniform float uDebugOverlayStrength;

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

    if (uDebugColorMode == 1)
    {
        float highlight = clamp(vDebugHighlight, 0.0, 1.0);
        vec3 debugLit = vDebugColor * (0.45 + 0.45 * diffuse);
        debugLit += vDebugColor * highlight * 0.35;
        lighting = mix(lighting, debugLit, clamp(uDebugOverlayStrength, 0.0, 1.0));
    }

    FragColor = vec4(lighting, 1.0);
}
)";

bool NeedsMeshRebuild(const TerrainRenderSettings& previous, const TerrainRenderSettings& next)
{
    // 只比较会改变 mesh 拓扑或顶点位置的参数
    // Wireframe 和光照只影响渲染状态
    // 不应该导致 CPU mesh 重算
    return previous.TerrainSize != next.TerrainSize ||
           previous.HeightScale != next.HeightScale ||
           previous.UseTerrainLod != next.UseTerrainLod ||
           previous.TerrainLodAlgorithm != next.TerrainLodAlgorithm ||
           previous.RoamMaxDepth != next.RoamMaxDepth ||
           previous.RoamSplitThreshold != next.RoamSplitThreshold ||
           previous.RoamMergeThreshold != next.RoamMergeThreshold ||
           previous.RoamDistanceScale != next.RoamDistanceScale ||
           previous.RoamSplitBudget != next.RoamSplitBudget ||
           previous.RoamEnableLocalConstraints != next.RoamEnableLocalConstraints ||
           previous.RoamEnableTopologyValidation != next.RoamEnableTopologyValidation;
}

glm::vec3 NormalizeLightDirection(const glm::vec3& lightDirection)
{
    if (glm::dot(lightDirection, lightDirection) <= 0.000001F)
    {
        // 零向量会让 shader normalize 产生未定义结果
        // 这里回退到默认斜向光
        return glm::normalize(glm::vec3{-0.45F, -1.0F, -0.35F});
    }

    return glm::normalize(lightDirection);
}

std::unique_ptr<Algorithms::ITerrainLodAlgorithm> CreateTerrainLodAlgorithm(
    Algorithms::TerrainLodAlgorithmId algorithmId)
{
    if (algorithmId == Algorithms::TerrainLodAlgorithmId::ClassicCpuRoam)
    {
        return std::make_unique<Algorithms::ClassicRoam::ClassicRoamTerrainLodAlgorithm>();
    }

    if (algorithmId == Algorithms::TerrainLodAlgorithmId::DataOrientedCpuRoam)
    {
        return std::make_unique<Algorithms::DataOrientedRoam::DataOrientedRoamTerrainLodAlgorithm>();
    }

    return nullptr;
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

    // HeightMap 必须先加载
    // 后续规则网格和 ROAM builder 都依赖它的尺寸和采样
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
    // ApplySettings 允许 UI 每帧调用
    // 只有真正影响 mesh 的字段变化才设置 dirty flag
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
    if (!_settings.UseTerrainLod && !_meshDirty)
    {
        return true;
    }

    // ROAM 类算法是视点相关 LOD，但微小移动时复用 mesh 降低卡顿
    if (_settings.UseTerrainLod)
    {
        // rebuild 距离随 terrain size 放大
        // 大地形下同样的世界位移对 screen error 影响更小
        const float rebuildDistance = std::max(_settings.TerrainSize * RoamRebuildTerrainScale, MinRoamRebuildDistance);
        const glm::vec3 buildDelta = cameraPosition - _lastRoamBuildCameraPosition;
        const bool cameraMovedEnough = !_hasRoamBuildCameraPosition ||
                                       glm::dot(buildDelta, buildDelta) >= rebuildDistance * rebuildDistance;

        // 拓扑维护较重，静止或微小移动时复用上一帧 mesh
        if (!_meshDirty && !cameraMovedEnough)
        {
            return true;
        }

        return RebuildTerrainLod(cameraPosition, errorMessage);
    }

    return RebuildMesh(errorMessage);
}

void TerrainRenderer::RequestMeshRebuild()
{
    _meshDirty = true;
}

void TerrainRenderer::ResetTerrainLodAlgorithm()
{
    _terrainLodAlgorithm.reset();
    _terrainLodStats = {};
    _hasRoamBuildCameraPosition = false;
    _meshDirty = true;
}

void TerrainRenderer::Shutdown()
{
    // OpenGL 资源允许重复 Shutdown
    // 每个 id 删除后都清零
    // 析构和显式 Shutdown 可以共用同一路径
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

    // viewport 使用 drawable 尺寸而不是窗口逻辑尺寸
    // HiDPI 屏幕上两者可能不同
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
    _shader.SetInt("uDebugColorMode", static_cast<int>(_settings.DebugColorMode));
    _shader.SetFloat("uDebugOverlayStrength", _settings.DebugOverlayStrength);
    _shader.SetInt("uTerrainTexture", 0);

    glBindVertexArray(_vertexArrayId);

    // wireframe 是临时渲染状态
    // 绘制后必须恢复调用前 polygon mode
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
        // wireframe 是全局 OpenGL 状态
        // 绘制后必须恢复外部状态
        glPolygonMode(GL_FRONT_AND_BACK, static_cast<GLenum>(previousPolygonMode[0]));
    }

    glBindVertexArray(0);
    glBindTexture(GL_TEXTURE_2D, 0);
}

TerrainRenderStats TerrainRenderer::Stats() const
{
    TerrainRenderStats stats{};
    // GUI 只读取汇总后的 TerrainRenderStats
    // 避免面板直接依赖算法内部类型
    stats.HeightMapWidth = _heightMap.Width();
    stats.HeightMapHeight = _heightMap.Height();
    stats.VertexCount = _meshData.Vertices.size();
    stats.TriangleCount = _meshData.Indices.size() / 3U;
    stats.DrawCallCount = _initialized ? 1 : 0;
    stats.TerrainSize = _settings.TerrainSize;
    stats.HeightScale = _settings.HeightScale;
    stats.UseTerrainLod = _settings.UseTerrainLod;
    stats.TerrainLodAlgorithm = _settings.TerrainLodAlgorithm;
    stats.RoamNodeCount = _terrainLodStats.ActiveNodeCount;
    stats.RoamOriginalTriangleCount = _terrainLodStats.OriginalTriangleCount;
    stats.RoamSubdividedTriangleCount = _terrainLodStats.SubdividedTriangleCount;
    stats.RoamRebuiltTriangleCount = _terrainLodStats.RebuiltTriangleCount;
    stats.RoamActiveSplitCount = _terrainLodStats.ActiveSplitCount;
    stats.RoamSplitCount = _terrainLodStats.SplitCount;
    stats.RoamForcedSplitCount = _terrainLodStats.ForcedSplitCount;
    stats.RoamMergeCount = _terrainLodStats.MergeCount;
    stats.RoamCrackRiskCount = _terrainLodStats.CrackRiskCount;
    stats.RoamConstraintPassCount = _terrainLodStats.ConstraintPassCount;
    stats.RoamCandidatePeakCount = _terrainLodStats.CandidatePeakCount;
    stats.RoamRejectedSplitCount = _terrainLodStats.RejectedSplitCount;
    stats.RoamRejectedMergeCount = _terrainLodStats.RejectedMergeCount;
    stats.RoamTjunctionCount = _terrainLodStats.TjunctionCount;
    stats.RoamInvalidNeighborCount = _terrainLodStats.InvalidNeighborCount;
    stats.RoamInvalidTopologyCount = _terrainLodStats.InvalidTopologyCount;
    stats.RoamCpuWorkerCount = _terrainLodStats.CpuWorkerCount;
    stats.RoamCpuUtilizationPercent = _terrainLodStats.CpuUtilizationPercent;
    stats.RoamUpdateMilliseconds = _terrainLodStats.CpuUpdateMilliseconds;
    stats.RoamSplitMilliseconds = _terrainLodStats.SplitMilliseconds;
    stats.RoamMergeMilliseconds = _terrainLodStats.MergeMilliseconds;
    stats.RoamEmitMilliseconds = _terrainLodStats.EmitMilliseconds;
    stats.RoamValidateMilliseconds = _terrainLodStats.ValidateMilliseconds;
    stats.RoamMaxDepthReached = _terrainLodStats.MaxActiveDepth;
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
    // 规则网格保留为视觉和性能 baseline
    // ROAM 类算法都通过统一接口重建 CPU mesh
    if (_settings.UseTerrainLod)
    {
        return RebuildTerrainLod(_lastCameraPosition, errorMessage);
    }

    return RebuildRegularGrid(errorMessage);
}

bool TerrainRenderer::RebuildRegularGrid(std::string* errorMessage)
{
    // 规则网格会重置 ROAM 统计，避免 UI 显示上一次算法结果
    _meshData = Terrain::TerrainMeshBuilder::Build(_heightMap, _settings.TerrainSize, _settings.HeightScale);
    _terrainLodStats = {};
    // 从 ROAM 切回规则网格时清空持久拓扑
    // 再切回 ROAM 会从当前设置重新建立 root diamond
    _terrainLodAlgorithm.reset();
    _hasRoamBuildCameraPosition = false;
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

bool TerrainRenderer::RebuildTerrainLod(const glm::vec3& cameraPosition, std::string* errorMessage)
{
    if (_terrainLodAlgorithm == nullptr || _terrainLodAlgorithm->Info().Id != _settings.TerrainLodAlgorithm)
    {
        // renderer 只持有统一接口
        // 具体算法由 UI 选择的 TerrainLodAlgorithmId 决定
        // 切换算法时必须重建持久拓扑状态
        _terrainLodAlgorithm = CreateTerrainLodAlgorithm(_settings.TerrainLodAlgorithm);
        _hasRoamBuildCameraPosition = false;
    }

    if (_terrainLodAlgorithm == nullptr)
    {
        if (errorMessage != nullptr)
        {
            *errorMessage = "Selected terrain LOD algorithm is not available";
        }
        return false;
    }

    Algorithms::TerrainLodSettings lodSettings{};
    // TerrainRenderSettings 包含光照和 UI 状态
    // TerrainLodSettings 只传算法真正需要的控制变量
    lodSettings.TerrainSize = _settings.TerrainSize;
    lodSettings.HeightScale = _settings.HeightScale;
    lodSettings.MaxDepth = _settings.RoamMaxDepth;
    lodSettings.SplitThreshold = _settings.RoamSplitThreshold;
    lodSettings.MergeThreshold = _settings.RoamMergeThreshold;
    lodSettings.DistanceScale = _settings.RoamDistanceScale;
    lodSettings.SplitBudget = _settings.RoamSplitBudget;
    lodSettings.EnableLocalConstraints = _settings.RoamEnableLocalConstraints;
    lodSettings.EnableTopologyValidation = _settings.RoamEnableTopologyValidation;

    Algorithms::TerrainLodBuildInput buildInput{};
    buildInput.HeightMap = &_heightMap;
    buildInput.CameraPosition = cameraPosition;
    buildInput.Settings = lodSettings;

    Algorithms::TerrainLodRenderPacket renderPacket{};
    // TerrainRenderer 只消费统一算法接口输出，不直接依赖具体 builder
    if (!_terrainLodAlgorithm->BuildRenderData(buildInput, renderPacket, errorMessage))
    {
        return false;
    }

    _meshData = std::move(renderPacket.CpuMesh);
    _terrainLodStats = _terrainLodAlgorithm->Stats();
    // camera rebuild 位置只在算法成功后更新
    // 失败时下一帧仍会尝试基于旧 mesh 状态重建
    _lastRoamBuildCameraPosition = cameraPosition;
    _hasRoamBuildCameraPosition = true;

    if (_meshData.Vertices.empty() || _meshData.Indices.empty())
    {
        // 算法接口成功但没有 CPU mesh 仍然不能上传
        // 当前 renderer 还没有 GPU-only packet 分支
        if (errorMessage != nullptr)
        {
            *errorMessage = "Terrain LOD mesh build failed";
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
    // LOD mesh 会随相机更新
    // 规则网格只在参数变化时更新
    const GLenum bufferUsage = _settings.UseTerrainLod ? GL_DYNAMIC_DRAW : GL_STATIC_DRAW;
    glBufferData(
        GL_ARRAY_BUFFER,
        static_cast<GLsizeiptr>(_meshData.Vertices.size() * sizeof(Terrain::TerrainMeshVertex)),
        _meshData.Vertices.data(),
        bufferUsage);

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, _indexBufferId);
    glBufferData(
        GL_ELEMENT_ARRAY_BUFFER,
        static_cast<GLsizeiptr>(_meshData.Indices.size() * sizeof(std::uint32_t)),
        _meshData.Indices.data(),
        bufferUsage);

    glEnableVertexAttribArray(0);
    // attribute layout 必须和 TerrainMeshVertex 以及 shader location 保持一致
    // 新增字段时这里和 shader 要一起改
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

    glEnableVertexAttribArray(4);
    glVertexAttribPointer(
        4,
        3,
        GL_FLOAT,
        GL_FALSE,
        static_cast<GLsizei>(sizeof(Terrain::TerrainMeshVertex)),
        reinterpret_cast<const void*>(offsetof(Terrain::TerrainMeshVertex, DebugColor)));

    glEnableVertexAttribArray(5);
    glVertexAttribPointer(
        5,
        1,
        GL_FLOAT,
        GL_FALSE,
        static_cast<GLsizei>(sizeof(Terrain::TerrainMeshVertex)),
        reinterpret_cast<const void*>(offsetof(Terrain::TerrainMeshVertex, DebugHighlight)));

    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    // GL_ELEMENT_ARRAY_BUFFER 绑定记录在 VAO 中
    // 因此这里只解绑 ARRAY_BUFFER
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
        // texture id 复用可以支持未来热重载
        glGenTextures(1, &_textureId);
    }

    // 纹理强制上传为 RGBA8
    // source channel count 不进入 shader 分支
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

    // stb 分配的像素内存必须在上传后释放
    stbi_image_free(pixels);
    return true;
}
} // 命名空间 ParallelRoam::Render
