# Parallel ROAM 项目计划与当前状态

> 项目类型：实时高度图地形 LOD 与现代并行拓扑实验
>
> 技术栈：C++20、CMake、SDL2、OpenGL 4.1、Direct3D 12、GLM、Dear ImGui、GLSL/HLSL
>
> 状态更新：2026-08-29；三算法最终实现、CBT 官方语义基线和最终性能分析已经完成

## 1. 当前结论

主分支现在包含三条可运行算法路径：Classic CPU ROAM、Data-Oriented CPU ROAM 和 D3D12 CBT 2024。Classic 与 DOD 使用相同的 ROAM 误差、双阈值和活动三角形硬预算；CBT 使用官方投影面积分类与固定 OCBT 容量，并已接入相同的应用、UI、调试着色、统计和 runtime benchmark 链路。

CBT 2024 阶段 A-I 已全部完成：四档 OCBT、基础半边、split/merge、双向传播、Reduce/Indexation、高度图几何、`ExecuteIndirect`、延迟诊断、逐阶段 GPU 计时、应用接入、自动测试和官方语义基线均已落地。旧 GPU ROAM-like 只保留在 `archive/gpu-roam-like` 分支，不属于当前构建与性能结论。

2026-08-28 的 Release/D3D12 最终实验在 1280×720 下对默认与极限路径各运行五轮。三角形平均规模已经校准到同一量级；DOD 相对 Classic 的平均帧耗时分别降低 26.15% 和 41.04%，CBT 相对 Classic 分别降低 93.24% 和 99.53%。完整 P95/P99、阶段时间和异常轮敏感性见[最终实验数据分析与结论](benchmark-output/runtime-benchmark-final-analysis-20260828.md)。

## 2. 已完成范围

| 工作流 | 最终状态 |
| --- | --- |
| Classic CPU ROAM | 嵌套楔形厚度、保守屏幕投影、持久 `Q_s/Q_m`、forced split、diamond merge、固定预算、增量 Mesh 与拓扑验证已完成 |
| Data-Oriented CPU ROAM | SoA/索引拓扑、批量评分、条件并行、持久双队列、持续预算交换、持久增量 Mesh 和阶段细分计时已完成；dirty 比例自适应全量 emit 未纳入最终基线 |
| D3D12 渲染迁移 | SDL2/D3D12 后端、Agility SDK/DXC 固定版本、普通 CPU Mesh 和 CBT 程序化 indirect draw 已完成 |
| CBT 2024 | 阶段 A-I 已完成，官方语义基线 v1 已冻结 |
| UI 与运行时观测 | 三算法选择、参数、活动三角形、最大实际深度、split 红色/merge 绿色、本帧 LOD 变化和分阶段耗时已接入 |
| Benchmark | 默认/极限离散路径、三算法轮转、Markdown/CSV、P95/P99、CBT 18 阶段 GPU 时间和最终分析已完成 |
| 自动验证 | C++/HLSL 注释率、CPU 单测、D3D12 四容量 smoke、runtime quick、基线清单与资源契约已接入 CTest |

## 3. 冻结基线与复现入口

- CBT 官方语义基线：[`docs/parallel-roam/18-cbt-2024-official-baseline-v1.md`](docs/parallel-roam/18-cbt-2024-official-baseline-v1.md)
- CBT 集成完成记录：[`docs/parallel-roam/16-cbt-2024-integration-plan.md`](docs/parallel-roam/16-cbt-2024-integration-plan.md)
- 最终实验结论：[`benchmark-output/runtime-benchmark-final-analysis-20260828.md`](benchmark-output/runtime-benchmark-final-analysis-20260828.md)
- 历史课程技术报告：[`docs/parallel-roam/final-technical-report.docx`](docs/parallel-roam/final-technical-report.docx)，冻结保留，不随当前 CBT 实现更新

正式 CBT 容量矩阵从冻结标签运行：

```powershell
git switch --detach benchmark/cbt-2024-official-baseline-v1
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/benchmark/d3d12/run_cbt_2024_official_baseline_v1.ps1
```

当前主分支三算法 runtime benchmark 使用 D3D12 Release/RelWithDebInfo 构建运行 `ParallelROAM.exe --runtime-benchmark`，报告输出到 `benchmark-output/`。默认路径固定 20K CPU 预算、128K CBT 和 58 px²；极限路径固定 200K CPU 预算、1M CBT 和 2.05 px²。

## 4. 当前研究边界

以下内容没有被最终实现冒充为已完成能力：

- CBT 容量限制的是动态二分器池，不是逐帧三角形硬预算；
- CBT v1 仍采用上游先到先分配语义，没有全局收益排序、共享闭包去重或严格预算优化；
- Classic/DOD 与 CBT 使用不同内部评分器，相同三角形数不等于相同画质；
- 项目尚未建立跨算法公共离线屏幕误差、深度误差和法线误差评估器；
- CBT 上游许可证仍未确认，source-referenced 部分不得作为公开可再分发实现发布；
- 正式实验只有两个高度图场景和一套主要硬件，不能外推到所有 DEM、GPU 或分辨率。

## 5. 后续工作

后续开发不再修改 `cbt_2024_official_baseline_v1` 的算法身份。所有调度实验必须使用新的算法键、shader 变体、清单和 benchmark 标签。

1. 建立独立于算法决策器的离线质量评估器，并增加小规模精确参考；
2. 在冻结 CBT 拓扑上提取 split 候选兼容闭包，度量真实成本与闭包重叠；
3. 对比先到先分配、误差 Top-K、独立成本贪心和闭包去重贪心；
4. 在独立 GPU 变体中实现确定性选择与严格预算提交；
5. 通过 H1-H4 后，再研究池饱和时的 merge/split 交换、迟滞和时间预算；
6. 扩充 DEM、遮挡后显露、快速跳转、分辨率和跨硬件重复实验。

继续与停止条件见[研究假设与验证计划](docs/parallel-roam/12-research-hypothesis-validation-plan.md)。

## 6. 文档地图

| 文档 | 作用 |
| --- | --- |
| [04-milestones.md](docs/parallel-roam/04-milestones.md) | 课程阶段和后续主线的时间线 |
| [05-experiments-and-benchmarks.md](docs/parallel-roam/05-experiments-and-benchmarks.md) | 当前 benchmark 口径、字段与最终结果摘要 |
| [09-development-guidelines.md](docs/parallel-roam/09-development-guidelines.md) | 目录、命名、注释、提交和验证规范 |
| [10-dependency-setup.md](docs/parallel-roam/10-dependency-setup.md) | OpenGL/D3D12 依赖、构建、运行和测试入口 |
| [11-bug-fix-log.md](docs/parallel-roam/11-bug-fix-log.md) | 已确认问题与修复证据 |
| [12-research-hypothesis-validation-plan.md](docs/parallel-roam/12-research-hypothesis-validation-plan.md) | 后续预算调度研究问题和可证伪假设 |
| [13-dx12-render-pipeline-migration-plan.md](docs/parallel-roam/13-dx12-render-pipeline-migration-plan.md) | 已完成的 D3D12 迁移历史和当前边界 |
| [15-large-cbt-architecture-reference.md](docs/parallel-roam/15-large-cbt-architecture-reference.md) | 上游 CBT 架构、语义与本项目映射 |
| [16-cbt-2024-integration-plan.md](docs/parallel-roam/16-cbt-2024-integration-plan.md) | 阶段 A-I 设计、实现结果、测试和源码索引 |
| [17-dod-classic-optimization-plan.md](docs/parallel-roam/17-dod-classic-optimization-plan.md) | DOD/Classic 优化过程与最终瓶颈 |
| [18-cbt-2024-official-baseline-v1.md](docs/parallel-roam/18-cbt-2024-official-baseline-v1.md) | 不可变 CBT v1 身份、容量矩阵和许可证门禁 |

旧课程范围、早期架构、GPU ROAM-like 技术报告和交付方案保留在[历史文档索引](docs/parallel-roam/history/README.md)，只用于追溯，不代表当前代码结构。
