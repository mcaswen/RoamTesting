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
/// 单帧渲染上下文，承载相机矩阵、投影深度约定和当前 drawable 尺寸
/// </summary>
struct RenderContext
{
    glm::mat4 View{1.0F}; // 世界空间到观察空间矩阵
    glm::mat4 Projection{1.0F}; // 与后端深度约定匹配的投影矩阵
    glm::vec3 CameraPosition{0.0F}; // LOD 误差评估使用的世界空间位置
    glm::vec3 CameraForward{0.0F, 0.0F, -1.0F}; // 可见性和视向调试使用的方向
    int DrawableWidth{1}; // HiDPI 后实际渲染宽度
    int DrawableHeight{1}; // HiDPI 后实际渲染高度
    bool UsesZeroToOneDepth{false}; // D3D12 为 true，OpenGL 为 false
};

/// <summary>
/// terrain renderer 的可编辑运行参数，来自 GUI 面板并驱动 mesh 重建或 shader uniform 更新
/// </summary>
struct TerrainRenderSettings
{
    float TerrainSize{30.0F}; // 地形世界空间边长
    float HeightScale{4.0F}; // 归一化高度的世界空间幅度
    bool Wireframe{false}; // 选择线框 PSO 或 OpenGL polygon mode
    TerrainDebugColorMode DebugColorMode{TerrainDebugColorMode::Lit}; // 地表着色模式
    float DebugOverlayStrength{0.85F}; // LOD 调试色与光照结果混合比例
    bool UseTerrainLod{true}; // false 时回退规则网格基线
    // UseTerrainLod 为 false 时算法 id 保留上次选择
    Algorithms::TerrainLodAlgorithmId TerrainLodAlgorithm{Algorithms::TerrainLodAlgorithmId::ClassicCpuRoam};
    int RoamMaxDepth{14}; // ROAM 二叉树深度硬上限

    // SplitThreshold 是进入细分的高水位阈值
    float RoamSplitThreshold{0.04F};

    // MergeThreshold 是回落粗网格的低水位阈值
    float RoamMergeThreshold{0.02F};
    float RoamDistanceScale{24.0F}; // 距离衰减对误差评分的影响

    // 局部约束只做 baseNeighbor forced split，不执行全局 repair
    bool RoamEnableLocalConstraints{true};

    // 拓扑验证会触发全局扫描，只用于 debug
    bool RoamEnableTopologyValidation{false};

    // 光照参数只影响表现，不触发 terrain mesh 重建
    glm::vec3 LightDirection{-0.45F, -1.0F, -0.35F}; // 指向地表的世界空间光线方向
    glm::vec3 LightColor{1.0F, 0.96F, 0.88F}; // 直接光颜色
    float AmbientStrength{0.28F}; // 环境光系数
    float DiffuseStrength{0.85F}; // 漫反射系数
    float SpecularStrength{0.18F}; // 镜面反射系数
};

/// <summary>
/// terrain renderer 汇总给 GUI 的渲染规模、ROAM 拓扑和各 pass 耗时统计
/// </summary>
struct TerrainRenderStats
{
    // HeightMapPath 记录 benchmark 实际使用的资源路径
    std::filesystem::path HeightMapPath; // 当前构建实际使用的高度图
    int HeightMapWidth{0}; // 高度图像素宽度
    int HeightMapHeight{0}; // 高度图像素高度
    std::size_t VertexCount{0}; // 当前绘制包顶点数量
    std::size_t TriangleCount{0}; // 当前绘制包三角形数量

    // 当前渲染器仍然保持单 draw call 提交 terrain
    int DrawCallCount{0}; // 本帧地形 draw 提交次数
    float TerrainSize{0.0F}; // 统计帧使用的世界空间边长
    float HeightScale{0.0F}; // 统计帧使用的高度缩放
    bool UseTerrainLod{false}; // 当前是否启用自适应算法
    Algorithms::TerrainLodAlgorithmId TerrainLodAlgorithm{Algorithms::TerrainLodAlgorithmId::ClassicCpuRoam}; // 实际算法标识
    std::string TerrainLodStatusMessage; // 算法输出模式和回退原因

    // setting 字段来自 UI 配置，用于和实际运行结果分开记录
    int RoamMaxDepthSetting{0}; // 用户配置的深度上限
    // 误差阈值和距离权重决定同一深度上限下的细分积极程度
    float RoamSplitThreshold{0.0F}; // 当前 split 高水位
    float RoamMergeThreshold{0.0F}; // 当前 merge 低水位
    float RoamDistanceScale{0.0F}; // 当前误差距离权重
    std::size_t RoamNodeCount{0}; // 节点池有效节点数量
    std::size_t RoamOriginalTriangleCount{0}; // 保持根级状态的活动三角形
    std::size_t RoamSubdividedTriangleCount{0}; // 由 split 产生的活动三角形
    std::size_t RoamRebuiltTriangleCount{0}; // 本次 emit 重新生成的三角形

    // 活跃 split 表示当前拓扑里仍展开的 internal triangle
    std::size_t RoamActiveSplitCount{0}; // 当前仍展开的内部节点数量

    // 常规 split 来自误差阈值和相机距离
    std::size_t RoamSplitCount{0}; // 本次更新的常规 split 提交数

    // 强制 split 来自 baseNeighbor diamond 传播
    std::size_t RoamForcedSplitCount{0}; // 兼容约束触发的额外 split 数

    // Merge 统计用于观察 hysteresis 是否稳定
    std::size_t RoamMergeCount{0}; // 本次更新成功 merge 数

    // CrackRisk 表示最大深度处仍无法继续修复的边界风险
    std::size_t RoamCrackRiskCount{0}; // 无法继续细分的边界风险数

    // ConstraintPass 表示 baseNeighbor 约束传播了多少次
    std::size_t RoamConstraintPassCount{0}; // 约束传播迭代或提交数量

    // CandidatePeakCount 用于观察 priority queue 是否过度膨胀
    std::size_t RoamCandidatePeakCount{0}; // 候选容器峰值长度

    // RejectedSplitCount 表示约束传播或过期候选导致的 split 失败
    std::size_t RoamRejectedSplitCount{0}; // 过期或不兼容的 split 候选数
    std::size_t RoamRejectedMergeCount{0}; // 过期或不满足 diamond 的 merge 候选数

    // TjunctionCount 只在拓扑验证开启后更新
    std::size_t RoamTjunctionCount{0}; // 验证器发现的 T junction 数

    // InvalidNeighborCount 用于定位 neighbor 指针互反关系错误
    std::size_t RoamInvalidNeighborCount{0}; // 邻接互反关系错误数

    // InvalidTopologyCount 用于定位 parent / child / root diamond 不变量错误
    std::size_t RoamInvalidTopologyCount{0}; // 父子和根菱形不变量错误数

    // CPU worker 和占用率用于观察并行路径是否真正生效
    std::size_t RoamCpuWorkerCount{0}; // 更新中实际使用的最大 worker 数
    float RoamCpuUtilizationPercent{0.0F}; // LOD build 区间的进程 CPU 利用率

    // 下列耗时用于拆分 CPU LOD 的成本来源
    float RoamTotalMilliseconds{0.0F}; // 完整 LOD build 墙钟时间
    float RoamUpdateMilliseconds{0.0F}; // 算法拓扑更新时间
    float RoamCpuUploadMilliseconds{0.0F}; // CPU 写入 GPU 资源时间
    float RoamSplitMilliseconds{0.0F}; // split 决策与提交时间
    float RoamMergeMilliseconds{0.0F}; // merge 决策与提交时间
    float RoamEmitMilliseconds{0.0F}; // CPU 或 GPU 网格生成时间
    float RoamValidateMilliseconds{0.0F}; // 可选拓扑验证时间
    float RoamGpuComputeMilliseconds{0.0F}; // GPU 时间戳测得的 compute 时间
    float RoamGpuSnapshotBuildMilliseconds{0.0F}; // CPU SoA 到 GPU 记录打包时间
    float RoamGpuBufferAllocationMilliseconds{0.0F}; // 本帧资源扩容时间
    float RoamGpuDispatchWallMilliseconds{0.0F}; // CPU 录制或发出 dispatch 的墙钟时间
    float RoamGpuQueryWaitMilliseconds{0.0F}; // OpenGL query 结果等待时间
    float RoamGpuReadbackWaitMilliseconds{0.0F}; // counter 回读等待时间
    float RoamFrameFenceWaitMilliseconds{0.0F}; // D3D12 帧资源 fence 等待时间
    float RoamRenderMilliseconds{0.0F}; // terrain draw 录制或提交时间
    std::size_t RoamCpuGpuUploadBytes{0}; // 本次 build 的 CPU 到 GPU 字节数
    std::size_t RoamCpuGpuReadbackBytes{0}; // 本次统计消费的 GPU 到 CPU 字节数

    // reached depth 是算法在当前相机和误差阈值下真正展开到的深度
    int RoamMaxDepthReached{0}; // 当前拓扑实际最大活动深度
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
    bool UpdateForView(const RenderContext& context, std::string* errorMessage);

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
    bool RebuildTerrainLod(const RenderContext& context, std::string* errorMessage);
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
    IGraphicsBackend* _graphicsBackend{nullptr}; // 借用 Application 持有的后端
    Terrain::HeightMap _heightMap; // CPU 高度采样源
    Terrain::TerrainMeshData _meshData; // CPU mesh 路径的最新输出
    std::unique_ptr<Algorithms::ITerrainLodAlgorithm> _terrainLodAlgorithm; // 当前 LOD 实例
    Algorithms::TerrainLodStats _terrainLodStats; // 最近成功 build 的算法统计
    std::string _terrainLodStatusMessage; // 最近成功 build 的输出说明
    float _terrainLodTotalMilliseconds{0.0F}; // renderer 外层 build 时间
    float _terrainLodCpuUploadMilliseconds{0.0F}; // renderer mesh 上传时间
    TerrainRenderSettings _settings; // 已应用的运行参数
    std::filesystem::path _heightMapPath; // 当前高度图资源路径
    std::filesystem::path _texturePath; // 当前地表纹理路径
    RenderContext _lastRenderContext{}; // 重建和 draw 共用的最近上下文
    glm::vec3 _lastRoamBuildCameraPosition{0.0F}; // 相机位移缓存基准
#if defined(PARALLEL_ROAM_GRAPHICS_API_OPENGL)
    unsigned int _vertexArrayId{0}; // CPU 和 GPU mesh 共用的 VAO
    unsigned int _vertexBufferId{0}; // renderer 拥有的 CPU mesh VBO
    unsigned int _indexBufferId{0}; // renderer 拥有的 CPU mesh IBO
    std::size_t _vertexBufferCapacityBytes{0}; // CPU VBO 当前容量
    std::size_t _indexBufferCapacityBytes{0}; // CPU IBO 当前容量
    unsigned int _textureId{0}; // renderer 拥有的地表纹理
    unsigned int _gpuVertexBufferId{0}; // 算法借出的 GPU VBO
    unsigned int _gpuIndexBufferId{0}; // 算法借出的 GPU IBO
    unsigned int _gpuIndirectDrawBufferId{0}; // 算法借出的间接命令缓冲
#endif
    Algorithms::TerrainLodRenderMode _renderMode{Algorithms::TerrainLodRenderMode::CpuMesh}; // 当前 draw 数据来源
    std::size_t _drawVertexCount{0}; // CPU 非索引路径顶点数
    std::size_t _drawIndexCount{0}; // CPU 或 GPU 索引数
    std::size_t _drawTriangleCount{0}; // GUI 和 benchmark 使用的绘制数量
    bool _initialized{false}; // renderer 资源是否完整
    bool _meshDirty{true}; // 下一次 UpdateForView 是否强制重建
    bool _hasRoamBuildCameraPosition{false}; // 相机缓存是否已建立
};
} // namespace ParallelRoam::Render
