# 面向固定拓扑预算的 GPU 自适应网格细分：兼容闭包感知的全局资源分配

本项目是一个面向高度图地形的自适应网格细分研究与实验平台。项目以 ROAM 1997 核心算法思想为基础，实现了 Classic CPU ROAM、Data-Oriented CPU ROAM 和 GPU ROAM-like 三条对照路径，并正在复现与适配 CBT 2024 的 GPU 动态拓扑方法。在此基础上，项目拟研究固定拓扑池容量下兼顾视觉收益、兼容闭包成本与 GPU 并行性的全局资源分配方法。

**研究状态：基线建设与问题定义阶段。**

ROAM 对照平台、统一误差口径、固定预算、拓扑验证、双图形后端和阶段化 benchmark 已可运行。CBT 2024 忠实基线正在复现，当前已完成 OCBT、基础二分器状态和程序化间接绘制。兼容闭包感知调度器已完成研究问题、假设、基线和实验计划定义，但尚未实现和验证。

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
| Data-Oriented CPU ROAM | SoA 节点池、索引邻接、持久 `Q_s/Q_m`、批量评分与条件并行 | 与 Classic 使用相同的误差公式、阈值、预算和拓扑验证口径；并行刷新队列优先级，局部维护队列成员，并持续执行预算交换直至队首条件收敛 | 拓扑依赖限制并行度，最终仍生成 CPU Mesh |
| GPU ROAM-like | CPU DOD 持久拓扑快照 + compute shader | GPU 叶三角形 compaction、误差评估、候选标记、单轮 split、Mesh emit、indirect draw | 混合管线；GPU merge 只评分不提交，GPU split 不回写 CPU 持久真值 |
| CBT 2024 | D3D12、OCBT 位域/归约树、GPU 基础二分器资源 | 四档 OCBT、基础拓扑、程序化间接绘制及专项验证 | 动态 split/merge、兼容传播和高度图自适应路径尚待实现 |
| 闭包感知预算调度器 | 计划中的独立研究变体 | 研究问题、假设、基线和验收标准已定义 | 尚未实现；必须在忠实 CBT 基线冻结后开展 |

### 统一误差、预算与拓扑语义

三种 ROAM 路径当前共享：

- ROAM 1997 论文第 6.1 节，自底向上预计算嵌套楔形厚度的公式 (1)；
- ROAM 1997 论文第 6.2 节，将楔形厚度转换为屏幕空间几何畸变上界的保守投影的公式 (2)/(3)；
- 楔形误差包围体穿越近裁剪面时的人工最大优先级；
- 完整 `ViewProjection`、可绘制区域宽度/高度和六平面视锥输入；
- 像素单位的 split/merge 双阈值；
- 活动叶三角形 `TriangleBudget` 上限；
- forced split、diamond merge 和可选拓扑验证；
- 为避免几何误差接近零时平坦区域过度粗糙，额外提供独立的投影边密度约束。

当前 Classic 路径已经对齐以下 ROAM 1997 关键机制：

- 公式 (1) 的嵌套楔形厚度预计算；
- 公式 (2)/(3) 的保守屏幕空间投影；
- 连续二叉三角树与 diamond 拓扑；
- forced split 和 diamond merge；
- 持久 split/merge 优先级队列；
- 增量 indexed CPU Mesh 更新。

DOD 也持久维护同口径的 `Q_s/Q_m`。预算满载时，只要 `max(Q_s) > min(Q_m)`，就先回收最低损失 diamond，再重试最高收益 split，直到队首条件不再成立。它保留全局候选排序与局部队列成员更新，但由于没有实现论文的全部单调性前提和优先级延期机制，仍不声称复现 ROAM 1997 的最少拓扑操作或最优网格证明。

当前实现未复现以下机制：

- priority deferral list；
- 原论文的三角形条带输出结构；
- 严格的 O(Delta N) 整帧更新复杂度；
- 给定预算下的数学最优性证明。

### 已完成的工作

- C++20、CMake 与 SDL2 应用框架；
- OpenGL 4.3 和 D3D12 双渲染后端；
- Dear ImGui 参数、LOD 调试着色与阶段化性能面板；
- CPU Mesh、GPU buffer 和 indirect draw 统一渲染包；
- 基于 ITerrainLodAlgorithm 统一接口实现并优化 Classic、DOD 和 GPU ROAM-like 路径；
- Classic、DOD 和 GPU ROAM-like 的阶段化 CPU/GPU 性能统计；
- 自动相机路径 benchmark，输出中文 Markdown 与逐帧 CSV；
- CTest、预算重入测试、GPU smoke test 和 CBT 专项 smoke test；
- 拓扑预算、邻接、T-junction 和资源契约检查。

## 快速开始

### 环境要求

- C++20 编译器；
- CMake 3.24 或更高版本；
- OpenGL 4.3 驱动，或 Windows D3D12 环境；
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
powershell -ExecutionPolicy Bypass -File scripts/run_relwithdebinfo_fetch.ps1
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

# GPU ROAM-like：32 帧 compute、compaction、emit 和 indirect draw
.\build\relwithdebinfo-fetch\bin\ParallelROAM.exe --gpu-smoke-test

# D3D12 CBT 专项验证
.\build\relwithdebinfo-d3d12-fetch\bin\ParallelROAM.exe --cbt-ocbt-smoke-test
.\build\relwithdebinfo-d3d12-fetch\bin\ParallelROAM.exe --cbt-base-topology-smoke-test
.\build\relwithdebinfo-d3d12-fetch\bin\ParallelROAM.exe --cbt-procedural-smoke-test
```

当前单元与结构测试覆盖嵌套楔形误差、保守屏幕投影、D3D/OpenGL 视锥约定、CBT 占用树、基础二分器拓扑、Classic/DOD 预算重入、Classic 增量 Mesh 输出及 C++ 注释覆盖率。

## Benchmark

### 无窗口算法回归

```powershell
.\build\relwithdebinfo-fetch\bin\ParallelROAM.exe `
  --benchmark `
  --algorithm all `
  --profile standard
```

可选算法为 `classic|dod|gpu|all`，profile 为 `smoke|budget-reentry|incremental-emit|standard`。`incremental-emit` 用三个相同 Classic 视点验证首帧初始化、一次调试属性过渡和随后零 dirty-range 复用；无图形上下文时 GPU 路径可能被跳过。

### 应用级运行时实验

```powershell
.\build\relwithdebinfo-fetch\bin\ParallelROAM.exe --runtime-benchmark
```

运行时 benchmark 会在同一配置下依次让可用算法走完相同的离散相机采样点，关闭 VSync，并在 `benchmark-output/` 生成：

- `runtime-benchmark-<timestamp>.md`：中文汇总和阶段对比；
- `runtime-benchmark-<timestamp>.csv`：逐帧原始数据。

默认选项路径包含 600 个采样点，极限压力路径包含 64 个采样点；每种算法都按相同 `sampleIndex` 执行，因此算法快慢不会再改变路径采样密度。可通过 `--runtime-benchmark-heightmap`、`--runtime-benchmark-terrain-size`、`--runtime-benchmark-height-scale`、`--runtime-benchmark-max-depth`、`--runtime-benchmark-split-pixels`、`--runtime-benchmark-merge-pixels`、`--runtime-benchmark-samples` 和 `--runtime-benchmark-label` 覆盖实验参数。旧 `--runtime-benchmark-duration` 仅作为兼容参数保留，每个名义秒换算为 60 个离散采样点。

正式对比实验将为每条算法路径独立执行 warm-up，并轮换或随机化算法运行顺序，以降低缓存状态、GPU 频率和设备温度造成的顺序偏差。

> ROAM 1997 论文公式 (1) 和公式 (2)/(3) 接入后，候选优先级分数（score）与活动拓扑语义已经变化。旧版本的三角形数量、score 分布等结果不应与当前版本直接横向比较。

实验方法、字段定义与现有 A/B 结果见[实验与基准测试](docs/parallel-roam/05-experiments-and-benchmarks.md)。

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
│   ├── algorithms/          Classic、DOD、GPU ROAM-like、CBT 2024
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
| [CBT 2024 接入计划](docs/parallel-roam/16-cbt-2024-integration-plan.md) | 忠实基线的阶段状态、能力边界和后续 split/merge 路线 |
| [实验与基准测试](docs/parallel-roam/05-experiments-and-benchmarks.md) | 指标、统计口径、实验流程和现有 A/B 结果 |
| [依赖配置说明](docs/parallel-roam/10-dependency-setup.md) | OpenGL/D3D12 依赖、固定版本和构建脚本 |

## 可复现实验要求

正式性能或质量结论至少应固定并记录：

- Git commit、构建配置和图形后端；
- CPU、GPU、驱动、D3D12Core/OpenGL 版本；
- HeightMap、`TerrainSize`、`HeightScale` 和 `MaxDepth`；
- split/merge 像素阈值与拓扑预算；
- 可绘制区域分辨率、FOV、相机路径和 VSync 状态；
- warm-up、路径采样点数、重复次数和随机种子；
- CPU/GPU 各逻辑阶段耗时；
- 最大/P95/平均屏幕误差、预算利用率、拓扑错误和确定性；

## 当前限制

- 兼容闭包感知调度器目前仅完成问题定义、假设、基线和实验计划，算法实现与 H1–H5 验证尚未开展；
- CBT 2024 当前只到基础拓扑和程序化绘制，完整动态 split/merge 尚未接入；
- GPU ROAM-like 是 CPU DOD + GPU split-only/emit 的混合路径，仅供当前实验探索和参考；
- Classic 已实现公式 (1)-(3)、连续二叉三角树/diamond 拓扑、持久双队列和增量 Mesh 输出；最终 priority、硬预算和输出格式是项目变体，当前研究不追求补齐 ROAM 1997 论文完整最优性证明；
- 当前正式场景数量和 DEM 多样性不足，旧性能数据也需要在新误差口径下重跑；

## 路线图

1. 完成并冻结 CBT 2024 忠实基线的 split、merge、兼容传播、槽位回收和高度图几何；
2. 建立公共离线质量评估器与小规模精确参考；
3. 在冻结拓扑上建立 split 候选兼容闭包，并对比先到先分配、误差 Top-K、独立成本贪心和闭包去重贪心；
4. 在独立 GPU 变体中实现闭包分析、确定性选择和严格预算提交，并与小规模精确参考比较；
5. 在 split-only 调度验证完成后，进一步研究池饱和状态下的低损失 merge、高收益 split 交换、迟滞和时间预算。

## 引用与参考

- ROAM 1997：Real-time Optimally Adapting Meshes；
- CBT 2020：Concurrent Binary Trees；
- CBT 2024：Large-scale Adaptive Mesh Refinement on the GPU；
- `third_party/LibGenROAM010206`：ROAM 1997 论文配套参考源码；
- `third_party/large_cbt`：CBT 2024 官方实现快照。

## 许可证

项目自有代码采用 [MIT License](LICENSE)。`third_party/` 中的依赖和参考项目保留各自许可证与使用条件。
