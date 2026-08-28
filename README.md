# 面向固定拓扑预算的自适应网格细分：兼容闭包感知的全局资源分配

本项目是一个面向高度图地形的自适应网格细分研究与实验平台。项目以 ROAM 1997 核心算法思想为基础，实现 Classic CPU ROAM、Data-Oriented CPU ROAM 两条对照路径，并完成 CBT 2024 GPU 动态拓扑在高度图地形、D3D12 渲染、运行时界面和 benchmark 链路中的接入。

**研究状态：三算法最终实现与性能基线已经完成，后续进入预算调度研究阶段。**

ROAM 对照平台、统一误差口径、固定预算、拓扑验证、双图形后端和阶段化 benchmark 均已可运行。CBT 2024 阶段 A-I 已完成，包含四档 OCBT、split/merge、双向传播、GPU 高度图几何、程序化间接绘制、延迟诊断、UI、自动测试与官方语义基线冻结。

![Parallel ROAM 交互界面](docs/parallel-roam/report-assets/进入后界面.png)

## 项目简介

CBT 2024 论文主要解决的是 GPU 动态拓扑问题，包括大量二分器的并发分配、拓扑依赖链并行传播等。其工作已经体现出，动态 GPU 拓扑的执行流程通常可以通过原子计数保证不超出内存池预算，但这并不等价于有限资源被分配给了最重要的细分候选。这一最优分配问题也不是 CBT 2024 论文的主要目标。当候选需求超过容量时，先到先得的提交顺序可能产生优先级倒置。与此同时，一次 split 可能需要沿邻接关系传播强制细分，多个候选的兼容闭包还可能互相重叠。

**本项目研究的问题是：**

在固定二分器池容量和实时 GPU 时间约束下，能否联合考虑 split 候选的视觉收益、兼容闭包依赖、闭包重叠和真实资源成本，在保持无裂缝拓扑的前提下选择价值更高的拓扑操作集合，从而改善有限预算下的最终网格质量？

**本项目的研究目标是：**

设计一种严格不超出拓扑池容量、保证兼容闭包完整、结果可解释且适合 GPU 并行执行的近似调度方法。

目前规划中，第一阶段聚焦于冻结当前拓扑后的 split-only 预算分配；后续阶段再研究池饱和状态下的 merge/split 容量交换。

若候选 split i 的预测收益为 `g_i`，其兼容闭包 `P_i` 表示执行该候选时，为保持无裂缝拓扑而新增的全部必要 split 操作，并包含候选 `i` 本身。对于已选择的候选集合 `S`：

```text
closure(S) = union(P_i), i in S
cost(S) = |closure(S)|
```

候选 i 相对于当前选择集合 S 的边际成本为：

```text
marginal_cost(i | S) = |P_i - closure(S)|
```

因此，第一阶段的优化目标可以近似为：

```text
maximize predicted_gain(S)
subject to cost(S) <= available_capacity
```

## 研究假设

项目围绕以下可证伪假设展开：

| 假设 | 验证目标 |
| --- | --- |
| H1 质量 | 在相同活动叶三角形数量或相同二分器池容量下，降低最大值、P95 和平均屏幕空间误差 |
| H2 预算 | 通过共享闭包去重降低保守预留产生的未使用容量，并保证拓扑池容量越界次数为零 |
| H3 调度 | 相比无序原子预留，稳定选择减少低收益候选提前占用预算的现象，并降低不同运行之间的结果方差 |
| H4 性能 | 在相同帧时间下获得更低误差，或在达到相同误差水平时使用更少的拓扑资源 |
| H5 动态性 | 相机快速移动或跳转后，merge/split 交换降低误差恢复时间和过渡期间的累计屏幕空间误差 |

详细的继续条件、停止条件和实验设计见[研究假设与验证计划](docs/parallel-roam/12-research-hypothesis-validation-plan.md)。

## 当前实现

### 算法路径

| 路径 | 数据与执行方式 | 当前能力 | 边界 |
| --- | --- | --- | --- |
| Classic CPU ROAM | 对象式节点、裸指针二叉三角树、串行索引堆 | 持久 `Q_s/Q_m`、统一交叉调度、split、forced split、diamond merge、视锥感知、固定叶三角形预算、增量 indexed CPU Mesh | 采用工程等价复现口径：公式、连续拓扑和增量输出对应 ROAM 1997 论文主要效果，但不追求完整最优性证明 |
| Data-Oriented CPU ROAM | SoA 节点池、索引邻接、持久 `Q_s/Q_m`、批量评分与条件并行 | 与 Classic 使用相同的误差公式、阈值、预算和拓扑验证口径；并行刷新队列优先级，局部维护队列成员，并持续执行预算交换直至队首条件收敛 | 拓扑依赖限制并行度；使用持久增量 CPU Mesh，尚未按 dirty 比例自动切换全量 emit |
| CBT 2024 | D3D12、OCBT 位域/归约树、GPU 常驻二分器资源 | 128K/256K/512K/1M、面积分类、split/merge、双向传播、高度图增量/全量几何、`ExecuteIndirect`、延迟诊断及 18 阶段 GPU 计时 | 官方语义基线 v1 已冻结；面积阈值控制细分，容量不是三角形硬预算 |
| 闭包感知预算调度器 | 计划中的独立研究变体 | 研究问题、假设、冻结基线和验收标准已定义 | 尚未实现；必须使用新的算法键、shader 变体和实验标签 |

### 统一误差、预算与拓扑语义

两种 CPU ROAM 路径当前共享：

- ROAM 1997 论文第 6.1 节，自底向上预计算嵌套楔形厚度的公式 (1)；
- ROAM 1997 论文第 6.2 节，将楔形厚度转换为屏幕空间几何畸变上界的保守投影的公式 (2)/(3)；
- 楔形误差包围体穿越近裁剪面时的人工最大优先级；
- 完整 `ViewProjection`、可绘制区域宽度/高度和六平面视锥输入；
- 像素单位的 split/merge 双阈值；
- 活动叶三角形 `TriangleBudget` 上限；
- forced split、diamond merge 和可选拓扑验证；
- 为避免几何误差接近零时平坦区域过度粗糙，额外提供独立的投影边密度约束。

当前实现未复现以下机制：

- priority deferral list；
- 原论文的三角形条带输出结构；
- 严格的 O(Delta N) 整帧更新复杂度；
- 给定预算下的数学最优性证明。

### 已完成的工作

- C++20、CMake 与 SDL2 应用框架；
- OpenGL 4.1 和 D3D12 双渲染后端；
- Dear ImGui 参数、LOD 调试着色与阶段化性能面板；
- CPU Mesh、CBT 程序化 indirect draw 统一渲染包；
- 基于 ITerrainLodAlgorithm 统一接口实现并优化 Classic、DOD 和 CBT 路径；
- Classic、DOD 的阶段化 CPU 性能统计；
- 自动相机路径 benchmark，输出中文 Markdown 与逐帧 CSV；
- CTest、预算重入/增量输出回归和 CBT 四容量专项 smoke test；
- 拓扑预算、邻接、T-junction 和资源契约检查。

## 快速开始

### 环境要求

- C++20 编译器；
- CMake 3.24 或更高版本；
- OpenGL 4.1 驱动，或 Windows D3D12 环境；
- Shader Model 6.6、64 位整数运算能力；
- Visual Studio C++ 工具链和 Windows SDK。

仓库包含 SDL2、GLM、GLAD、Dear ImGui、stb 和 Windows portable CMake。普通 preset 会优先使用系统包，再使用项目内 `third_party`，默认不主动联网。

### OpenGL

```powershell
.\tools\cmake\bin\cmake.exe --preset relwithdebinfo-fetch
.\tools\cmake\bin\cmake.exe --build --preset relwithdebinfo-fetch --parallel
.\build\relwithdebinfo-fetch\bin\ParallelROAM.exe
```

也可以使用快捷脚本：

```powershell
powershell -ExecutionPolicy Bypass -File scripts/run/opengl/run_relwithdebinfo_fetch.ps1
```

### D3D12

D3D12 构建固定使用项目指定版本的 Agility SDK 和 DXC。若本地固定依赖尚未准备，先运行：

```powershell
powershell -ExecutionPolicy Bypass -File scripts/setup_cbt_dx12_dependencies.ps1
```

然后配置和运行：

```powershell
.\tools\cmake\bin\cmake.exe --preset relwithdebinfo-d3d12-fetch
.\tools\cmake\bin\cmake.exe --build --preset relwithdebinfo-d3d12-fetch --parallel
.\build\relwithdebinfo-d3d12-fetch\bin\ParallelROAM.exe
```

也可以使用对应的 D3D12 快捷脚本：

```powershell
powershell -ExecutionPolicy Bypass -File scripts/run/d3d12/run_relwithdebinfo_fetch.ps1
```

依赖版本、离线策略和平台差异见[依赖配置说明](docs/parallel-roam/10-dependency-setup.md)。

## 交互操作

| 操作 | 输入 |
| --- | --- |
| 前后左右移动 | `W` / `S` / `A` / `D` |
| 上升/下降 | `Space` / `Ctrl` |
| 加速移动 | `Shift` |
| 旋转视角 | 按住鼠标右键并移动 |
| 退出 | `Esc` |

右侧面板可以切换高度图、线框、LOD 算法和调试着色，并调整 `TerrainSize`、`HeightScale`、`MaxDepth`、`Split/Merge thresholds (px)`、`TriangleBudget`、局部约束、拓扑验证与光照参数。

CBT 的运行时界面与 Classic/DOD 使用同一条算法选择、统计和调试显示链路。本帧 split 三角形显示为红色，merge 三角形显示为绿色，并在左侧信息面板中显示容量、活动深度、动态槽位、诊断样本年龄和各 GPU 阶段耗时。

## 测试与验证

运行 CTest：

```powershell
.\tools\cmake\bin\ctest.exe `
  --test-dir build\relwithdebinfo-fetch `
  -C RelWithDebInfo `
  --output-on-failure
```

常用自动验证入口：

```powershell
# 窗口、资源和基础渲染
.\build\relwithdebinfo-fetch\bin\ParallelROAM.exe --smoke-test

# D3D12 CBT 专项验证
.\build\relwithdebinfo-d3d12-fetch\bin\ParallelROAM.exe --cbt-ocbt-smoke-test
.\build\relwithdebinfo-d3d12-fetch\bin\ParallelROAM.exe --cbt-base-topology-smoke-test
.\build\relwithdebinfo-d3d12-fetch\bin\ParallelROAM.exe --cbt-procedural-smoke-test
```

当前单元与结构测试覆盖嵌套楔形误差、保守屏幕投影、D3D/OpenGL 视锥约定、CBT 占用树、基础二分器拓扑、分类、split 规划与提交、simplify、地形几何、Classic/DOD 预算重入与增量 Mesh 输出，以及 C++/HLSL 注释覆盖率。D3D12 CTest 还覆盖四档 CBT 容量、默认/极限 runtime quick、FullDebug 几何和冻结清单验证。

## Benchmark

### 无窗口算法回归

```powershell
.\build\relwithdebinfo-fetch\bin\ParallelROAM.exe `
  --benchmark `
  --algorithm all `
  --profile standard
```

可选算法为 `classic|dod|all`，profile 为 `smoke|budget-reentry|incremental-emit|standard`。`incremental-emit` 用三个相同视点验证首帧初始化、一次属性过渡和随后零 dirty-range 复用。

### 应用级运行时实验

```powershell
.\build\relwithdebinfo-fetch\bin\ParallelROAM.exe --runtime-benchmark
```

运行时 benchmark 会在同一配置下依次让可用算法走完相同的离散相机采样点，关闭 VSync，并在 `benchmark-output/` 生成。D3D12 构建在设备支持时自动加入 CBT，OpenGL 构建继续运行 Classic 与 DOD：

- `runtime-benchmark-<timestamp>.md`：中文汇总和阶段对比；
- `runtime-benchmark-<timestamp>.csv`：逐帧原始数据。

默认选项路径包含 600 个采样点，固定使用 20K 预算、20 层最大深度、128K CBT 容量和 58 px² 面积阈值；极限压力路径包含 64 个采样点，固定使用 200K 预算、20 层最大深度、1M CBT 容量和 2.05 px² 面积阈值。两条路径都已按稳态活动三角形规模校准，每种算法按相同 `sampleIndex` 执行，因此算法快慢不会改变路径采样密度。性能路径默认使用 `Off` 验证与 `ModifiedOnly` 几何；仍可通过 `--runtime-benchmark-path`、`--runtime-benchmark-heightmap`、`--runtime-benchmark-terrain-size`等显式覆盖实验参数。

实验方法、字段定义与当前对比结果见[实验与基准测试](docs/parallel-roam/05-experiments-and-benchmarks.md)。2026-08-28 的最终 Release/D3D12 数据与分阶段结论见[最终实验数据分析与结论](benchmark-output/runtime-benchmark-final-analysis-20260828.md)。

## 项目结构

```text
.
├── assets/                  高度图、字体和 D3D12/HLSL shader
├── benchmark-output/        本地 runtime benchmark 与实验输出
├── cmake/                   CMake 依赖和编译配置
├── docs/
│   ├── parallel-roam/       当前计划、实验、架构和修复记录
│   └── source_analysis/     ROAM 1997 论文、参考实现和源码上下文分析
├── scripts/                 构建、运行、smoke test 与报告脚本
├── src/
│   ├── algorithms/          Classic、DOD、CBT 2024
│   ├── app/                 主循环、相机和 runtime benchmark
│   ├── benchmark/           无窗口 benchmark 与 probe
│   ├── gui/                 ImGui 控制和统计面板
│   ├── render/              OpenGL/D3D12 后端和 terrain renderer
│   └── terrain/             HeightMap 与 CPU mesh 数据
├── tests/                   CTest 单元和性质测试
├── third_party/             固定第三方源码和参考项目
├── CMakeLists.txt
└── CMakePresets.json
```

## 文档索引

| 文档 | 内容 |
| --- | --- |
| [研究假设与验证计划](docs/parallel-roam/12-research-hypothesis-validation-plan.md) | 研究问题、H1-H5、基线、继续/停止条件 |
| [CBT 2024 接入计划](docs/parallel-roam/16-cbt-2024-integration-plan.md) | 阶段 A-I 完成记录、架构、能力边界与源码索引 |
| [CBT 2024 官方语义基线 v1](docs/parallel-roam/18-cbt-2024-official-baseline-v1.md) | 冻结身份、四容量结果、上游差异和许可证门禁 |
| [实验与基准测试](docs/parallel-roam/05-experiments-and-benchmarks.md) | 当前指标、统计口径、实验流程和三算法结论 |
| [历史课程技术报告](docs/parallel-roam/final-technical-report.docx) | 冻结的 OpenGL 课程阶段 DOCX，不随当前 CBT 实现更新 |
| [依赖配置说明](docs/parallel-roam/10-dependency-setup.md) | OpenGL/D3D12 依赖、固定版本和构建脚本 |

## 可复现实验要求

正式性能或质量结论至少应固定并记录：

- Git commit、构建配置和图形后端；
- CPU、GPU、驱动、D3D12Core/OpenGL 版本；
- HeightMap、`TerrainSize`、`HeightScale` 和 `MaxDepth`；
- split/merge 像素阈值与拓扑预算；
- 可绘制区域分辨率、FOV、相机路径和 VSync 状态；
- warm-up、路径采样点数、重复次数和随机种子；
- 各逻辑阶段耗时和 renderer 上传开销；
- 最大/P95/平均屏幕误差、预算利用率、拓扑错误和确定性；

## 当前限制

- 兼容闭包感知调度器目前仅完成问题定义、假设、基线和实验计划，算法实现与 H1–H5 验证尚未开展；
- CBT 2024 官方语义基线已冻结，但它仍采用上游先到先分配语义，容量是二分器池上限而不是严格三角形预算；
- CBT 极限路径通过面积阈值把平均工作量校准到约 20 万三角形，逐帧仍会在约 18.3 万至 21.7 万之间变化；跨算法质量结论仍需要独立离线误差评估器；
- GPU ROAM-like 已移至 `archive/gpu-roam-like` 分支，不属于主分支构建目标；
- Classic 已实现公式 (1)-(3)、连续二叉三角树/diamond 拓扑、持久双队列和增量 Mesh 输出；最终 priority、硬预算和输出格式是项目变体，当前研究不追求补齐 ROAM 1997 论文完整最优性证明；
- 当前正式场景仅覆盖 Test129 与 Peking 两条路径，DEM 多样性、遮挡场景和跨硬件重复仍需扩充；

## 路线图

1. 建立公共离线质量评估器与小规模精确参考；
2. 在冻结 CBT v1 上建立 split 候选兼容闭包，并对比先到先分配、误差 Top-K、独立成本贪心和闭包去重贪心；
3. 在独立 GPU 变体中实现闭包分析、确定性选择和严格预算提交，并与小规模精确参考比较；
4. 在 split-only 调度验证完成后，研究池饱和状态下的低损失 merge、高收益 split 交换、迟滞和时间预算；
5. 扩充 DEM、视点变化和跨硬件重复实验，报告质量、预算、性能与稳定性共同结论。

## 引用与参考

- ROAM 1997：Real-time Optimally Adapting Meshes；
- CBT 2020：Concurrent Binary Trees；
- CBT 2024：Large-scale Adaptive Mesh Refinement on the GPU；
- `third_party/LibGenROAM010206`：ROAM 1997 论文配套参考源码；
- `third_party/large_cbt`：CBT 2024 官方实现快照。

## 许可证

项目自有代码采用 [MIT License](LICENSE)。`third_party/` 中的依赖和参考项目保留各自许可证与使用条件。
