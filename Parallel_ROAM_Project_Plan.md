# Parallel ROAM Project Plan

> 课程：计算机图形学  
> 项目类型：结课大作业 / 综合性图形学实验  
> 技术栈：C++20、SDL2、OpenGL 4.3+、GLM、CMake、Dear ImGui、可选 EnTT / 自定义线程池  
> 核心主题：ROAM 地形 LOD、数据导向设计、多线程、Compute Shader、GPU-Driven Rendering

这个文件现在只作为项目计划入口。原来的 1000+ 行单体文档已经拆分到 `docs/parallel-roam/`，便于按主题阅读和后续维护。

## 文档地图

| 文档 | 作用 | 建议阅读时机 |
|---|---|---|
| [01-project-scope.md](docs/parallel-roam/01-project-scope.md) | 项目定位、问题背景、目标分层、非目标 | 开工前、和老师/队友对齐范围时 |
| [02-theory-and-architecture.md](docs/parallel-roam/02-theory-and-architecture.md) | ROAM 术语、误差度量、裂缝处理、技术栈、接口和公共数据 | 写核心代码前 |
| [03-implementation-strategies.md](docs/parallel-roam/03-implementation-strategies.md) | Classic CPU、Data-Oriented CPU、GPU ROAM-like 三个实现方案 | 进入算法实现阶段时 |
| [04-milestones.md](docs/parallel-roam/04-milestones.md) | 阶段 0 到阶段 5 的任务和验收标准 | 每周规划和进度检查时 |
| [05-experiments-and-benchmarks.md](docs/parallel-roam/05-experiments-and-benchmarks.md) | 实验假设、Debug View、Benchmark 方法、指标和数据记录 | 开始性能测试与写报告前 |
| [06-risks-and-delivery.md](docs/parallel-roam/06-risks-and-delivery.md) | 风险降级、最终演示流程、课程提交清单、评分标准、报告结构、完成定义 | 中后期控范围和准备展示时 |
| [07-reference-projects.md](docs/parallel-roam/07-reference-projects.md) | GitHub 参考项目、SDL2 迁移参考点、许可风险 | 做 SDL2 / OpenGL 工程迁移和算法对照时 |
| [08-engine-framework.md](docs/parallel-roam/08-engine-framework.md) | 多算法切换、GUI、渲染、性能分析的底层框架 | 开始铺代码架构前 |
| [09-development-guidelines.md](docs/parallel-roam/09-development-guidelines.md) | 目录、命名、注释、Git、个人流程和 AI 使用规范 | 提交文件前 |
| [10-dependency-setup.md](docs/parallel-roam/10-dependency-setup.md) | 跨平台依赖方案、vcpkg 与 FetchContent 路径 | 配置 Win/mac 开发环境时 |

## 一句话定位

Parallel ROAM 是一个面向现代并行硬件的实时地形自适应细分实验系统：它以经典 ROAM 为基础，对比传统 CPU、数据导向多核 CPU 与 GPU Compute 三种实现范式，研究动态地形 LOD 在不同架构下的性能、数据布局与拓扑维护权衡。

## 原文各部分合理性评估

| 原章节 | 合理性判断 | 本次处理 |
|---|---|---|
| 1. 项目概述 | 方向清楚，适合作为项目定位；但不应承载实现细节。 | 精简到本入口和范围文档。 |
| 2. 核心问题与项目目标 | 合理，能解释为什么做 LOD；目标层级需要更明确。 | 拆成基础目标、对比目标、高分目标、冲刺目标和非目标。 |
| 3. 理论与术语约定 | 必要且正确，是后续实现共同语言；可补充 Height Map 尺寸约束和误差稳定性。 | 移入理论架构文档，并增加实现注意事项。 |
| 4. 开发环境与初始框架 | 技术选择合理；完整目录树放在总计划里过长。 | 移入架构文档，改为建议结构而不是一次性强约束。 |
| 5. 总体架构划分 | 分层和策略接口合理，是三版本对比的关键。 | 保留并强化公共接口、统计数据和公平对比边界。 |
| 6. 三个实现版本的定位 | 核心价值最高；需要明确 GPU 版是 ROAM-like，不强求复刻经典优先队列。 | 单独成文，加入成熟度等级和推荐交付线。 |
| 7. 实现规划与里程碑 | 很实用，但占据篇幅最大，应成为执行清单。 | 单独成文，按阶段保留任务和验收标准。 |
| 8. 预期研究结论与可验证假设 | 合理，但应避免预设结论。 | 改写为实验假设，移入实验文档。 |
| 9. 风险控制与降级策略 | 必须保留，能防止范围失控。 | 移入风险交付文档，增加触发信号和降级线。 |
| 10. 最终演示流程建议 | 对答辩有用，但属于交付材料。 | 移入风险交付文档。 |
| 11. 技术报告对应结构建议 | 合理，和实验数据强相关。 | 移入风险交付文档，并和完成定义对齐。 |
| 12. 当前优先级清单 | 适合放在入口，便于快速扫描。 | 保留在本文件，做成短清单。 |
| 13. 项目一句话定位 | 有价值，适合作为总标题下的摘要。 | 保留在本文件。 |

## 当前实施主线

1. 先做一个稳定可截图的渲染闭环：SDL2 + OpenGL + ImGui + FPS 相机 + Height Map 地形。
2. 再做 Classic CPU ROAM：先 split-only，再处理 base neighbor / forced split，merge 和 hysteresis 后置。
3. Data-Oriented CPU 版优先验证 SoA、index-based node pool、并行 error evaluation 和 active leaf collection。
4. GPU 版的主目标定为 Level B：GPU error evaluation + GPU candidate marking + CPU topology；active leaf compaction 和 indirect draw 作为高分项。
5. 全部实验必须使用相同 Height Map、相机路径、误差阈值、分辨率和统计口径，否则三版本性能数据不可比。

## 当前优先级清单

### 必须完成

- [ ] SDL2 + CMake + OpenGL 基础工程；
- [ ] Height Map 地形显示；
- [ ] FPS 相机与 ImGui；
- [ ] 普通 CPU ROAM split-only；
- [ ] wireframe 与 active triangle stats；
- [ ] Data-Oriented index / SoA 重构；
- [ ] 多线程 error evaluation；
- [ ] Classic vs Data-Oriented CPU 性能对比；
- [ ] 技术报告素材与实验日志。

### 高分目标

- [ ] base neighbor / forced split；
- [ ] merge + hysteresis；
- [ ] LOD depth heatmap；
- [ ] GPU error evaluation；
- [ ] GPU candidate marking；
- [ ] OpenGL timer query；
- [ ] CSV 自动导出；
- [ ] 固定相机路径 benchmark。

### 冲刺目标

- [ ] GPU active leaf compaction；
- [ ] indirect draw；
- [ ] GPU split-only；
- [ ] GPU crack-free dependency propagation；
- [ ] terrain chunk 并行；
- [ ] Mesh Shader / 更高级 GPU pipeline 探索。
