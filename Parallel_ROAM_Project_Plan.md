# Parallel ROAM Project Plan

> 项目类型：实时地形 LOD 与现代并行拓扑实验
>
> 技术栈：C++20、CMake、SDL2、OpenGL 4.3、Direct3D 12、GLM、Dear ImGui、GLSL/HLSL
>
> 当前主线：D3D12 CBT 2024 忠实基线接入

Parallel ROAM 已完成 Classic CPU、Data-Oriented CPU 和 GPU ROAM-like 三条实验路线，以及 OpenGL/D3D12 双后端迁移。当前工作集中在 CBT 2024 的 GPU 常驻拓扑复现：OCBT、基础半边拓扑和程序化间接绘制已经落地，下一阶段是完整 split 路径。

## 当前状态

- Classic CPU ROAM：已完成完整方差树、像素 SSE、活动三角形预算、视锥感知、单 Build 级联合并、邻接约束和拓扑验证；
- Data-Oriented CPU ROAM：已完成索引节点池、SoA、多线程扫描、候选标记和保守并发拓扑提交，并与 Classic 对齐完整方差树、像素 SSE、视锥感知、20,000 活动 leaf 硬预算和单 Build 级联合并；
- GPU ROAM-like：OpenGL 与 D3D12 均已有计算、GPU 网格生成和间接绘制验证路径；
- D3D12 渲染迁移：阶段 0 至阶段 6 已关闭；
- CBT 2024：阶段 B、C、D 已完成，阶段 E split 待实施；
- 自动验证：包含注释覆盖率、OCBT、CBT 基础拓扑、视锥输入、Classic/DOD 六视点预算/视锥/级联 smoke 和多条 GPU smoke test；
- 性能基线：正式 runtime benchmark 当前仍比较 Classic、DOD 和 GPU ROAM-like，CBT 在完整拓扑迁移前不进入性能排名。

## 当前文档

| 文档 | 作用 |
|---|---|
| [04-milestones.md](docs/parallel-roam/04-milestones.md) | Classic、DOD、GPU ROAM-like 和实验阶段的完成记录 |
| [05-experiments-and-benchmarks.md](docs/parallel-roam/05-experiments-and-benchmarks.md) | 三算法性能实验口径与统计字段 |
| [07-reference-projects.md](docs/parallel-roam/07-reference-projects.md) | 早期 ROAM 与引擎参考项目索引 |
| [09-development-guidelines.md](docs/parallel-roam/09-development-guidelines.md) | 当前目录、命名、注释、Git 和开发流程规范 |
| [10-dependency-setup.md](docs/parallel-roam/10-dependency-setup.md) | OpenGL/D3D12 依赖、构建入口和验证命令 |
| [11-bug-fix-log.md](docs/parallel-roam/11-bug-fix-log.md) | 已确认问题及修复证据 |
| [12-research-hypothesis-validation-plan.md](docs/parallel-roam/12-research-hypothesis-validation-plan.md) | CBT 固定预算调度的研究问题与可证伪假设 |
| [13-dx12-render-pipeline-migration-plan.md](docs/parallel-roam/13-dx12-render-pipeline-migration-plan.md) | 已完成的 D3D12 迁移总结和边界 |
| [15-large-cbt-architecture-reference.md](docs/parallel-roam/15-large-cbt-architecture-reference.md) | large_cbt 官方实现的架构与算法路径参考 |
| [16-cbt-2024-integration-plan.md](docs/parallel-roam/16-cbt-2024-integration-plan.md) | 当前 CBT 2024 分阶段实施计划 |

## 历史文档

旧课程阶段的范围、架构、实施策略、交付方案和技术报告已移动到 [历史文档索引](docs/parallel-roam/history/README.md)。这些文件保留原始决策和实验结论，但不再作为当前代码结构或开发阶段的依据。

## 当前优先级

### 已完成

- [x] OpenGL 三算法实现、自动 benchmark 和技术报告；
- [x] OpenGL/D3D12 双后端与 D3D12 GPU ROAM-like；
- [x] CBT 2024 设备能力检测和程序化间接绘制；
- [x] 128K、256K、512K、1M OCBT CPU/GPU 对照；
- [x] 规则地形基础二分器、完整 GPU 资源集和容量切换验证。

### 当前主线

- [ ] 迁移 CBT 面积分类、视锥剔除和 split 候选生成；
- [ ] 实现兼容链规划、容量预留、空闲槽位分配和 split 提交；
- [ ] 重建活动索引并验证位域、heapID 和邻接不变量。

### 后续阶段

- [ ] 完成 merge、槽位回收和 split/merge 闭环；
- [ ] 接入高度图几何求值、法线、纹理和调试颜色；
- [ ] 补齐 CBT 参数、统计、运行时 benchmark 和官方基线冻结；
- [ ] 在忠实基线稳定后验证固定预算下的全局资源调度研究假设。

## 开发约束

1. CBT 官方基线和后续预算调度变体必须使用独立算法标识、shader 和 benchmark 标签；
2. 普通渲染帧不允许为了统计进行未说明的同步 GPU 读回；
3. OpenGL 三算法实验与 D3D12 CBT 实验分别保留可复现构建和数据口径；
4. 所有阶段声明以代码能力、自动测试和 `16-cbt-2024-integration-plan.md` 的状态为准。
