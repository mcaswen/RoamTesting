# Theory and Architecture

## Height Map 与二维参数域

Height Map 是二维数组或纹理：

```text
H(x, z) -> y
```

ROAM 的三角形拓扑首先在 `x-z` 平面内定义；渲染时，每个二维顶点通过 Height Map 采样获得高度：

```text
P(x, z) = (x, H(x, z), z)
```

因此，两个根三角形覆盖的是 Height Map 的二维矩形定义域，而不是一开始就精确覆盖复杂的三维山体表面。

实现建议：

- 首版优先使用 `(2^n + 1) x (2^n + 1)` 的 Height Map，方便二叉三角树递归到整数网格点；
- 将 terrain world size、height scale 和 Height Map texel 坐标映射写成独立函数；
- 所有版本共享同一套采样和坐标转换逻辑，避免实验时出现实现差异。

## ROAM 的二叉三角树

每个三角形沿 base edge 二分，生成两个子三角形：

```text
Parent Triangle
├── Left Child
└── Right Child
```

两棵根三角树共同覆盖整个地形区域。首版可以只保证 split 正确；邻接、merge、hysteresis 后续逐步加入。

## 误差度量

项目采用逐步增强的误差度量：

1. 距离误差：根据三角形中心到相机距离决定细分，用于快速验证树结构与渲染流程。
2. 几何误差：比较 Height Map 实际高度与当前三角形平面插值高度，可使用三角形中点、边中点或预计算最大偏差。
3. 屏幕空间误差：使用几何误差、距离、FOV 与屏幕尺寸估算视觉误差，作为三版本对比时的统一细分标准。

近似形式：

```text
screenError ~= geometricError * projectionScale / distanceToCamera
```

实现时需要注意：

- 对 `distanceToCamera` 做下限保护，避免相机贴近地形时误差爆炸；
- 使用同一套 projection 参数计算三版本的 screen-space error；
- 记录 error sampling mode，避免“看似同阈值、实际不同算法”的不公平对比。

## 邻接关系与裂缝

每个 ROAM 三角形需要维护：

```text
baseNeighbor
leftNeighbor
rightNeighbor
```

当一个三角形沿 base edge 细分后，base neighbor 若不匹配，可能产生 T-junction 和裂缝。项目建议逐步实现：

- 基础邻接检查；
- forced split；
- diamond split 或相邻层级差约束；
- 必要时使用 skirt 作为调试期的工程性保底方案。

合理路线是先做 split-only，再把裂缝处理作为可视化可验证的阶段目标，不要一开始就把完整 split/merge/neighbor 全部塞进同一轮实现。

## 技术基线

| 模块 | 方案 |
|---|---|
| 编程语言 | C++20 |
| 构建系统 | CMake |
| 窗口 / 输入 / OpenGL Context | SDL2 |
| OpenGL 函数加载 | GLAD 或 GLEW |
| 数学库 | GLM |
| 图像读取 | stb_image |
| GUI | Dear ImGui |
| 性能分析 | `std::chrono`、OpenGL Timer Query、可选 Tracy / RenderDoc |
| 数据导向参考 | EnTT 可选；核心 ROAM 节点建议自定义 SoA |
| GPU 并行 | OpenGL Compute Shader、SSBO、Atomic Counter、Indirect Draw |

建议使用 OpenGL 4.3+，因为其具备 Compute Shader、SSBO 和 Atomic Counter。若硬件与驱动允许，可使用 OpenGL 4.5 作为目标版本。

## 建议目录结构

这不是一次性强制完成的目录，而是稳定后可以收敛到的结构。

```text
ParallelROAM/
├── CMakeLists.txt
├── README.md
├── docs/
├── assets/
│   ├── heightmaps/
│   ├── textures/
│   └── shaders/
├── external/
├── src/
│   ├── app/
│   ├── render/
│   ├── terrain/
│   ├── roam/
│   ├── jobs/
│   ├── ui/
│   └── util/
└── tests/
```

建议优先稳定 `app`、`render`、`terrain`、`roam` 四个目录，再补 `jobs`、`ui`、`tests`。

## 分层架构

```text
+------------------------------------------------------+
| Application Layer                                    |
| SDL2 Window / Input / Main Loop / Camera / ImGui     |
+------------------------------------------------------+
| Visualization & Experiment Layer                     |
| Render Modes / Debug Views / Timers / CSV Logging    |
+------------------------------------------------------+
| Common Terrain Layer                                 |
| HeightMap / Error Metric / Terrain Config / Math     |
+------------------------------------------------------+
| ROAM Strategy Layer                                  |
| ClassicRoam | DataOrientedRoam | GpuRoam             |
+------------------------------------------------------+
| Execution Layer                                      |
| Single-thread CPU | Thread Pool | Compute Shader     |
+------------------------------------------------------+
| Rendering Layer                                      |
| OpenGL Terrain Rendering / SSBO / Draw Calls         |
+------------------------------------------------------+
```

## 策略接口

三种版本应共享一套统一接口，避免渲染、UI 和实验框架与具体实现耦合。

```cpp
class IRoamSystem
{
public:
    virtual ~IRoamSystem() = default;

    virtual void Initialize(const HeightMap& heightMap,
                            const TerrainConfig& config) = 0;

    virtual void Update(const CameraData& camera,
                        float deltaTime) = 0;

    virtual void BuildRenderData() = 0;

    virtual const RoamRenderData& GetRenderData() const = 0;

    virtual const RoamStatistics& GetStatistics() const = 0;

    virtual void Reset() = 0;
};
```

三种实现：

```text
ClassicRoam        : IRoamSystem
DataOrientedRoam   : IRoamSystem
GpuRoam            : IRoamSystem
```

如果代码里继续使用 `DotsRoam` 命名，需要在注释或文档里说明它表示 Data-Oriented CPU，不是 Unity DOTS 依赖。

## 公共数据结构

三版本应尽量统一概念数据，保证公平对比。

```text
TerrainConfig
- terrainWorldSize
- heightScale
- maxDepth
- splitThreshold
- mergeThreshold
- errorSamplingMode
- enableCrackFix
- workerThreadCount
```

```text
RoamStatistics
- activeTriangleCount
- activeNodeCount
- splitCount
- mergeCount
- maxActiveDepth
- averageActiveDepth
- cpuErrorEvalMs
- cpuDecisionMs
- cpuTopologyMs
- cpuCollectMs
- gpuComputeMs
- renderMs
- totalFrameMs
```

## 最小测试面

建议在早期加入轻量测试或调试断言：

- Height Map 坐标映射；
- triangle split 后两个 child 是否覆盖 parent；
- active leaf 面积总和是否覆盖根三角区域；
- neighbor link 是否互相一致；
- forced split 是否会无限递归；
- CPU 与 GPU error 抽样结果是否接近。

