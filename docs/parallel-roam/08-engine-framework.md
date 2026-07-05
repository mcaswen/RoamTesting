# Engine Framework

本项目底层框架的核心目标是：稳定外壳、可替换算法内核、统一观察与控制层。这样后续添加 Classic ROAM、Data-Oriented ROAM、GPU ROAM-like、CDLOD、geometry clipmap 等算法时，不需要重写 GUI、渲染、benchmark 和性能统计。

## 设计原则

1. 算法层只负责决定“生成什么地形细节”，不直接控制窗口、GUI 或 draw call。
2. 渲染层只负责“怎么画”，读取统一的渲染包或 GPU 资源描述。
3. GUI 层只改配置和触发命令，不直接修改算法内部节点。
4. Profiling 层统一记录 CPU、GPU、算法和渲染指标，不散落在各模块里。
5. Benchmark 层使用固定相机路径和固定配置，保证不同算法结果可比。
6. 所有跨模块交互优先通过接口、配置对象、事件或 controller 完成，避免互相硬引用。

## 推荐目录结构

```text
src/
├── app/            # 程序生命周期、主循环、窗口、输入、相机
├── platform/       # SDL2 / OpenGL context / 文件路径等平台细节
├── render/         # Shader、Buffer、Texture、TerrainRenderer、DebugRenderer
├── terrain/        # HeightMap、TerrainConfig、采样、坐标转换、地形数据集
├── algorithms/     # 多算法统一接口、注册、切换、算法实例
│   ├── classic_roam/
│   ├── data_oriented_roam/
│   └── gpu_roam/
├── gui/            # ImGui layer、panels、view models
├── profiling/      # CPU scopes、GPU timer query、frame stats、CSV export
├── benchmark/      # 固定相机路径、实验配置、自动跑分
├── resources/      # 资源加载、路径、shader include
└── util/           # logger、assert、math helpers、ring buffer

assets/
├── heightmaps/
├── textures/
├── shaders/
├── materials/
└── benchmark_paths/

docs/
└── parallel-roam/

external/
third_party/
tests/
```

`external/` 和 `third_party/` 只放明确来源的第三方代码或资源，不放项目业务代码。

## 主循环数据流

```text
Application::Run
  ↓
Input update
  ↓
Camera update
  ↓
AlgorithmController::Update
  ↓
Algorithm builds RenderPacket
  ↓
TerrainRenderer / DebugRenderer draw
  ↓
GuiLayer draws panels
  ↓
FrameProfiler finalizes frame
  ↓
Optional benchmark recording / CSV export
```

`Application` 是外壳，`AlgorithmController` 是算法切换入口，`Renderer` 是渲染出口，`FrameProfiler` 是性能数据出口。

## 多算法统一接口

接口不要命名为只服务 ROAM 的 `IRoamSystem`。建议使用更通用的 `ITerrainLodAlgorithm`，为后续非 ROAM 算法留空间。

```cpp
class ITerrainLodAlgorithm
{
public:
    virtual ~ITerrainLodAlgorithm() = default;

    virtual AlgorithmInfo GetInfo() const = 0;
    virtual AlgorithmCapabilities GetCapabilities() const = 0;

    virtual void Initialize(const TerrainDataset& dataset,
                            const TerrainConfig& config) = 0;

    virtual void Update(const FrameContext& frame,
                        const CameraData& camera) = 0;

    virtual void BuildRenderData(RenderPacket& outPacket) = 0;

    virtual const AlgorithmStats& GetStats() const = 0;
    virtual void DrawDebugGui() = 0;

    virtual void Reset() = 0;
};
```

建议提供以下辅助类型：

```text
AlgorithmInfo
- Id
- DisplayName
- Description
- Version

AlgorithmCapabilities
- SupportsCpuMeshOutput
- SupportsGpuDrivenRendering
- SupportsSplit
- SupportsMerge
- SupportsCrackFix
- SupportsDebugHeatmap

AlgorithmStats
- ActiveTriangleCount
- ActiveNodeCount
- SplitCount
- MergeCount
- MaxActiveDepth
- CpuErrorEvalMs
- CpuDecisionMs
- CpuTopologyMs
- CpuCollectMs
- GpuComputeMs
- CpuGpuUploadBytes
- CpuGpuReadbackBytes
```

## AlgorithmRegistry

算法注册和创建由 `AlgorithmRegistry` 负责，不在 GUI 或主循环里写 `switch`。

```text
AlgorithmRegistry
- RegisterFactory(AlgorithmId, Factory)
- GetAvailableAlgorithms()
- Create(AlgorithmId)

AlgorithmController
- SelectAlgorithm(AlgorithmId)
- ResetCurrentAlgorithm()
- UpdateCurrentAlgorithm()
- BuildRenderData()
- GetCurrentStats()
```

切换算法时应复用同一份 `TerrainDataset` 和 `TerrainConfig`，并重置 profiler history，避免新旧算法数据混在一起。

## 渲染边界

算法不直接调用 OpenGL draw。算法只输出 `RenderPacket` 或 GPU 资源描述。

```text
RenderPacket
- Mode
- CpuVertices
- CpuIndices
- GpuVertexBuffer
- GpuIndexBuffer
- ActiveLeafBuffer
- IndirectDrawBuffer
- DebugLines
- DebugOverlays
- ActiveTriangleCount
- MaterialId
```

渲染层根据 `RenderPacket::Mode` 选择路径：

```text
CpuMesh        -> VBO / IBO upload + DrawElements
GpuBuffers     -> bind existing buffers + DrawElements
GpuIndirect    -> bind indirect command + DrawElementsIndirect
DebugOnly      -> DebugRenderer draw lines / overlays
```

这样 Classic 版可以 CPU 生成 mesh，GPU 版可以输出 SSBO / indirect draw 信息，但 GUI 和主循环不需要跟着变。

## GUI 层

GUI 使用 ImGui，但不让 panel 直接操作算法内部数据。

```text
gui/
├── ImGuiLayer
├── MainMenuBar
├── TerrainPanel
├── AlgorithmPanel
├── RenderPanel
├── ProfilingPanel
├── BenchmarkPanel
└── DebugViewPanel
```

GUI 主要读写这些状态对象：

```text
AppState
TerrainConfig
RenderSettings
DebugViewSettings
BenchmarkSettings
ProfilerSnapshot
```

会改变系统状态的 UI 操作应变成明确命令：

```text
SelectAlgorithmCommand
ResetAlgorithmCommand
ReloadHeightMapCommand
StartBenchmarkCommand
StopBenchmarkCommand
ExportCsvCommand
```

## 性能分析层

Profiling 从第一版就内置，避免后期补指标时重构一大片。

```text
profiling/
├── CpuProfiler
├── ScopedCpuTimer
├── GpuTimerQuery
├── FrameProfiler
├── StatsHistory
└── CsvWriter
```

统一帧统计结构：

```text
FrameStats
- FrameIndex
- TimeSeconds
- AlgorithmId
- TotalFrameMs
- CpuUpdateMs
- CpuAlgorithmMs
- CpuErrorEvalMs
- CpuDecisionMs
- CpuTopologyMs
- CpuCollectMs
- CpuMeshBuildMs
- CpuUploadMs
- GpuComputeMs
- RenderMs
- ActiveTriangles
- ActiveNodes
- SplitCount
- MergeCount
- MaxActiveDepth
- CpuGpuUploadBytes
- CpuGpuReadbackBytes
```

算法内部只负责填自己的 `AlgorithmStats`，`FrameProfiler` 负责合并成 GUI 面板、历史曲线和 CSV。

## Benchmark 层

Benchmark 不依赖人工飞行操作，应支持固定相机路径回放。

```text
BenchmarkRunner
- LoadScenario()
- Start()
- Stop()
- Update()
- ExportCsv()

BenchmarkScenario
- HeightMapPath
- CameraPathPath
- TerrainConfig
- RenderSettings
- WarmupSeconds
- RecordSeconds
```

Benchmark 执行前后要记录完整配置，否则性能图无法复现。

## 推荐演进顺序

1. 完成 `Application / Window / Input / Camera / FrameContext`。
2. 完成 `ITerrainLodAlgorithm / AlgorithmRegistry / AlgorithmController`。
3. 加入 `DummyFixedGridAlgorithm`，先验证算法切换和渲染包链路。
4. 接入 ImGui，能切换算法、调 terrain 参数、显示 stats。
5. 接入 `FrameProfiler`，先 CPU 计时，再加 OpenGL Timer Query。
6. 实现 Classic ROAM split-only。
7. 实现 Data-Oriented ROAM。
8. 实现 GPU ROAM-like 的 compute 阶段。

## 模块边界红线

- `gui/` 不包含算法节点、OpenGL buffer ownership 或 benchmark 采样细节。
- `algorithms/` 不创建 SDL window，不处理 SDL event，不直接画 ImGui。
- `render/` 不决定 LOD，只消费 `RenderPacket`。
- `profiling/` 不依赖具体算法类型，只处理统一 stats。
- `terrain/` 不依赖 GUI 和 renderer，只提供数据、采样和坐标转换。
- `platform/` 封装 SDL2 / OpenGL context 细节，避免 SDL 头文件扩散到全项目。

