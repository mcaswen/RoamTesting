#pragma once

#include "algorithms/ITerrainLodAlgorithm.h"
#if defined(PARALLEL_ROAM_GRAPHICS_API_OPENGL)
#include "render/Shader.h"
#endif
#include "terrain/HeightMap.h"
#include "terrain/TerrainMeshBuilder.h"

#include <cstddef>
#include <filesystem>
#include <memory>
#include <string>

namespace ParallelRoam::Render
{
class IGraphicsBackend;
#if defined(PARALLEL_ROAM_GRAPHICS_API_D3D12)
struct D3D12TerrainRendererState;
#endif

/// <summary>
/// terrain shader 的调试着色模式
/// </summary>
enum class TerrainDebugColorMode
{
    Lit = 0,
    LodState = 1,
};

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

/// <summary>
/// terrain renderer 的可编辑运行参数，来自 GUI 面板并驱动 mesh 重建或 shader uniform 更新
/// </summary>
struct TerrainRenderSettings
{
    float TerrainSize{30.0F};
    float HeightScale{4.0F};
    bool Wireframe{false};
    TerrainDebugColorMode DebugColorMode{TerrainDebugColorMode::Lit};
    float DebugOverlayStrength{0.85F};
    bool UseTerrainLod{true};
    // UseTerrainLod 为 false 时算法 id 保留上次选择
    Algorithms::TerrainLodAlgorithmId TerrainLodAlgorithm{Algorithms::TerrainLodAlgorithmId::ClassicCpuRoam};
    int RoamMaxDepth{14};

    // SplitThreshold 是进入细分的高水位阈值
    float RoamSplitThreshold{0.04F};

    // MergeThreshold 是回落粗网格的低水位阈值
    float RoamMergeThreshold{0.02F};
    float RoamDistanceScale{24.0F};

    // 局部约束只做 baseNeighbor forced split，不执行全局 repair
    bool RoamEnableLocalConstraints{true};

    // 拓扑验证会触发全局扫描，只用于 debug
    bool RoamEnableTopologyValidation{false};

    // 光照参数只影响表现，不触发 terrain mesh 重建
    glm::vec3 LightDirection{-0.45F, -1.0F, -0.35F};
    glm::vec3 LightColor{1.0F, 0.96F, 0.88F};
    float AmbientStrength{0.28F};
    float DiffuseStrength{0.85F};
    float SpecularStrength{0.18F};
};

/// <summary>
/// terrain renderer 汇总给 GUI 的渲染规模、ROAM 拓扑和各 pass 耗时统计
/// </summary>
struct TerrainRenderStats
{
    // HeightMapPath 记录 benchmark 实际使用的资源路径
    std::filesystem::path HeightMapPath;
    int HeightMapWidth{0};
    int HeightMapHeight{0};
    std::size_t VertexCount{0};
    std::size_t TriangleCount{0};

    // 当前渲染器仍然保持单 draw call 提交 terrain
    int DrawCallCount{0};
    float TerrainSize{0.0F};
    float HeightScale{0.0F};
    bool UseTerrainLod{false};
    Algorithms::TerrainLodAlgorithmId TerrainLodAlgorithm{Algorithms::TerrainLodAlgorithmId::ClassicCpuRoam};
    std::string TerrainLodStatusMessage;

    // setting 字段来自 UI 配置，用于和实际运行结果分开记录
    int RoamMaxDepthSetting{0};
    // 误差阈值和距离权重决定同一深度上限下的细分积极程度
    float RoamSplitThreshold{0.0F};
    float RoamMergeThreshold{0.0F};
    float RoamDistanceScale{0.0F};
    std::size_t RoamNodeCount{0};
    std::size_t RoamOriginalTriangleCount{0};
    std::size_t RoamSubdividedTriangleCount{0};
    std::size_t RoamRebuiltTriangleCount{0};

    // 活跃 split 表示当前拓扑里仍展开的 internal triangle
    std::size_t RoamActiveSplitCount{0};

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

    // CandidatePeakCount 用于观察 priority queue 是否过度膨胀
    std::size_t RoamCandidatePeakCount{0};

    // RejectedSplitCount 表示约束传播或过期候选导致的 split 失败
    std::size_t RoamRejectedSplitCount{0};
    std::size_t RoamRejectedMergeCount{0};

    // TjunctionCount 只在拓扑验证开启后更新
    std::size_t RoamTjunctionCount{0};

    // InvalidNeighborCount 用于定位 neighbor 指针互反关系错误
    std::size_t RoamInvalidNeighborCount{0};

    // InvalidTopologyCount 用于定位 parent / child / root diamond 不变量错误
    std::size_t RoamInvalidTopologyCount{0};

    // CPU worker 和占用率用于观察并行路径是否真正生效
    std::size_t RoamCpuWorkerCount{0};
    float RoamCpuUtilizationPercent{0.0F};

    // 下列耗时用于拆分 CPU LOD 的成本来源
    float RoamTotalMilliseconds{0.0F};
    float RoamUpdateMilliseconds{0.0F};
    float RoamCpuUploadMilliseconds{0.0F};
    float RoamSplitMilliseconds{0.0F};
    float RoamMergeMilliseconds{0.0F};
    float RoamEmitMilliseconds{0.0F};
    float RoamValidateMilliseconds{0.0F};
    float RoamGpuComputeMilliseconds{0.0F};
    float RoamGpuSnapshotBuildMilliseconds{0.0F};
    float RoamGpuBufferAllocationMilliseconds{0.0F};
    float RoamGpuDispatchWallMilliseconds{0.0F};
    float RoamGpuQueryWaitMilliseconds{0.0F};
    float RoamGpuReadbackWaitMilliseconds{0.0F};
    float RoamFrameFenceWaitMilliseconds{0.0F};
    float RoamRenderMilliseconds{0.0F};
    std::size_t RoamCpuGpuUploadBytes{0};
    std::size_t RoamCpuGpuReadbackBytes{0};

    // reached depth 是算法在当前相机和误差阈值下真正展开到的深度
    int RoamMaxDepthReached{0};
};

/// <summary>
/// 负责上传和绘制规则网格高度图地形
/// </summary>
class TerrainRenderer
{
public:
    TerrainRenderer();
    ~TerrainRenderer();

    TerrainRenderer(const TerrainRenderer&) = delete;
    TerrainRenderer& operator=(const TerrainRenderer&) = delete;

    bool Initialize(
        IGraphicsBackend& graphicsBackend,
        const std::filesystem::path& heightMapPath,
        const std::filesystem::path& texturePath,
        const TerrainRenderSettings& settings,
        std::string* errorMessage);

    bool ApplySettings(const TerrainRenderSettings& settings, std::string* errorMessage);
    bool LoadHeightMap(const std::filesystem::path& heightMapPath, std::string* errorMessage);
    bool UpdateForCamera(const glm::vec3& cameraPosition, std::string* errorMessage);

    // benchmark 可绕过普通相机位移缓存，要求下一帧重新构建 mesh
    void RequestMeshRebuild();

    // 切换 benchmark 算法时清掉持久 ROAM 拓扑和上一帧统计
    void ResetTerrainLodAlgorithm();
    void Shutdown();
    void Render(const RenderContext& context);

    [[nodiscard]] TerrainRenderStats Stats() const;
    [[nodiscard]] const std::filesystem::path& HeightMapPath() const;
    [[nodiscard]] const std::filesystem::path& TexturePath() const;

private:
    bool RebuildMesh(std::string* errorMessage);

    // baseline 路径，便于和 ROAM 视觉对照
    bool RebuildRegularGrid(std::string* errorMessage);

    // Terrain LOD 路径会随相机位置动态更新
    bool RebuildTerrainLod(const glm::vec3& cameraPosition, std::string* errorMessage);
    bool UploadMesh(std::string* errorMessage);
#if defined(PARALLEL_ROAM_GRAPHICS_API_OPENGL)
    bool ConfigureTerrainVertexArray(
        unsigned int vertexBufferId,
        unsigned int indexBufferId,
        std::string* errorMessage);
    bool BindGpuTerrainBuffers(
        const Algorithms::TerrainLodRenderPacket& renderPacket,
        std::string* errorMessage);
#endif
    bool LoadTexture(const std::filesystem::path& texturePath, std::string* errorMessage);
    [[nodiscard]] bool HasDrawableTerrain() const;

#if defined(PARALLEL_ROAM_GRAPHICS_API_OPENGL)
    Shader _shader;
#elif defined(PARALLEL_ROAM_GRAPHICS_API_D3D12)
    std::unique_ptr<D3D12TerrainRendererState> _d3d12State;
#endif
    IGraphicsBackend* _graphicsBackend{nullptr};
    Terrain::HeightMap _heightMap;
    Terrain::TerrainMeshData _meshData;
    std::unique_ptr<Algorithms::ITerrainLodAlgorithm> _terrainLodAlgorithm;
    Algorithms::TerrainLodStats _terrainLodStats;
    std::string _terrainLodStatusMessage;
    float _terrainLodTotalMilliseconds{0.0F};
    float _terrainLodCpuUploadMilliseconds{0.0F};
    TerrainRenderSettings _settings;
    std::filesystem::path _heightMapPath;
    std::filesystem::path _texturePath;
    glm::vec3 _lastCameraPosition{0.0F};
    glm::vec3 _lastRoamBuildCameraPosition{0.0F};
#if defined(PARALLEL_ROAM_GRAPHICS_API_OPENGL)
    unsigned int _vertexArrayId{0};
    unsigned int _vertexBufferId{0};
    unsigned int _indexBufferId{0};
    std::size_t _vertexBufferCapacityBytes{0};
    std::size_t _indexBufferCapacityBytes{0};
    unsigned int _textureId{0};
    unsigned int _gpuVertexBufferId{0};
    unsigned int _gpuIndexBufferId{0};
    unsigned int _gpuIndirectDrawBufferId{0};
#endif
    Algorithms::TerrainLodRenderMode _renderMode{Algorithms::TerrainLodRenderMode::CpuMesh};
    std::size_t _drawVertexCount{0};
    std::size_t _drawIndexCount{0};
    std::size_t _drawTriangleCount{0};
    bool _initialized{false};
    bool _meshDirty{true};
    bool _hasRoamBuildCameraPosition{false};
};
} // 命名空间 ParallelRoam::Render
