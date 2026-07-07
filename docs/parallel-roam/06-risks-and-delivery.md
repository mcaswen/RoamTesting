# Risks and Delivery

## 风险 1：Classic ROAM 邻接关系难以调通

### 症状

- 裂缝；
- 重叠三角形；
- 三角形消失；
- 无限递归 forced split；
- merge 后邻接关系失效。

### 策略

1. 先做 split-only；
2. 先做小 Height Map 与低 maxDepth；
3. 增加 node id、depth、neighbor link 可视化；
4. 先使用相邻深度差约束；
5. 必要时先用 skirt 作为边界裂缝保底；
6. merge 放到后期。

### 降级线

如果完整 diamond split 无法稳定完成，保留 split-only + forced split + skirt，并在报告里说明限制。

## 风险 2：Data-Oriented 版用了多线程但更慢

### 原因

- 工作量太小；
- 任务切分过细；
- 锁竞争；
- false sharing；
- topology commit 占比过高；
- 内存访问仍不连续。

### 策略

- 将并行任务切成较大 chunks；
- 使用 thread-local buffer；
- 避免每节点创建任务；
- 单独统计每阶段耗时；
- 不强求全流程并行；
- 诚实分析瓶颈。

### 降级线

如果总帧耗时没有明显优化，仍可展示阶段级收益，例如 error evaluation 变快但 topology commit 成为瓶颈。这是合理的研究结论。

## 风险 3：GPU 动态拓扑难以完成

### 分级交付

```text
最低：GPU Error Evaluation
可交：GPU Error + Candidate Marking
高分：GPU Active Leaf Compaction + Indirect Draw
冲刺：GPU Split-only
最高：GPU Split/Merge + Crack-free
```

即使未完成完整 GPU topology，也可明确说明：

> GPU 版本聚焦于 ROAM 中可高度并行的误差评估与 LOD 决策阶段；复杂邻接拓扑更新保留在 CPU 或作为后续工作讨论。

这仍然是合理的系统设计结论。

## 风险 4：项目范围失控

### 不作为首轮目标

- 无限大 terrain streaming；
- 植被、河流、天气等复杂场景系统；
- PBR 全套材质；
- 完整 GPU priority queue；
- 多平台适配；
- 商业级地形编辑器。

### 策略

- 每个视觉功能必须服务于 LOD 展示；
- 每周至少产出一个可截图阶段成果；
- 报告材料和实验日志不要等到最后一周再整理；
- 冲刺目标失败时保留失败案例、截图和原因分析。

## 最终演示流程

1. 启动程序，展示普通实体地形；
2. 切换 wireframe，展示近细远粗；
3. 切换 LOD heatmap，展示细分层级；
4. 快速飞向山峰/峡谷，展示局部自适应；
5. 切换 Classic CPU / Data-Oriented CPU / GPU 模式；
6. 展示统计面板：active triangle count、CPU update、GPU compute、FPS、split / merge count；
7. 回放固定相机路径，显示三版本曲线；
8. 展示 Data-Oriented 版线程扩展性作为辅助实验；
9. 总结普通版的正确性与可读性、Data-Oriented 版的 CPU 并行收益、GPU 版的高并行优势与拓扑难点。

## 课程提交要求对齐

本节按课程要求整理最终交付物、报告结构和评分关注点。后续打包前应逐项检查，避免“代码能跑但交付材料缺项”。

### 提交内容清单

#### 1. 完整代码工程

- 提交可编译、可运行的完整工程文件；
- 保留 `src/`、`cmake/`、`docs/`、`scripts/`、`assets/`、必要 `third_party/` 依赖和 CMake preset；
- 清理编译生成的中间文件，例如 `build/`、`bin/`、`obj/`、`.vs/`、临时日志和本地 IDE 缓存；
- 保留必要运行资源，例如 height map、地表纹理、字体和报告截图；
- 核心算法必须体现自己的实现和理解，尤其是 Classic ROAM、Data-Oriented ROAM、GPU ROAM-like 的拓扑维护、误差评估、mesh emit 和性能统计；
- 若引用开源代码或第三方库，必须保留 license，并在代码注释或文档中说明来源与用途；
- 禁止直接提交网上下载的完整源码且没有实质性修改。

#### 2. 可执行演示程序

- C++ 项目应提供 Release 或 RelWithDebInfo 版本的可执行文件；
- Windows 打包时至少包含 `ParallelROAM.exe`、运行所需资源目录 `assets/`，以及如果采用动态链接时需要的 DLL；
- 当前项目优先使用 `relwithdebinfo-fetch` 作为演示构建，既保留符号便于排查，又接近 Release 性能；
- 若目标机器不支持 OpenGL 4.3 / Compute Shader，演示时应明确说明 GPU ROAM-like 会被 capability gate 跳过，不应静默伪装成 GPU 成绩。

#### 3. 技术开发报告

- 报告使用 A4 排版；
- 字数不少于 2500 字；
- 图文并茂，截图、表格和曲线必须能对应项目实际功能；
- 报告要体现对图形学原理、工程架构和性能瓶颈的理解，而不是只罗列界面功能。

### 技术开发报告建议结构

#### 第一章 绪论

- 项目背景：实时地形渲染、远近细节差异和固定网格浪费；
- 选题意义：ROAM LOD、现代 CPU / GPU 并行架构下的动态地形细分；
- 国内外研究现状：ROAM、Chunked LOD、Geometry Clipmap、GPU-driven terrain 等相关方法；
- 主要功能概述：Height Map 地形、FPS 相机、ImGui 调试面板、Classic / DOD / GPU 三算法切换、benchmark 输出。

#### 第二章 相关技术与理论基础

- Height Map 地形表示和采样；
- 变换矩阵、View / Projection、相机控制；
- 三角网格、顶点属性、法线和基础光照模型；
- 二叉三角树、split / merge、diamond 约束和裂缝处理；
- 几何误差与屏幕空间误差；
- 数据导向设计、SoA、线程池和并行任务划分；
- OpenGL 4.3 Compute Shader、SSBO、timer query 和 indirect draw。

#### 第三章 系统设计与实现（重点章节）

- 总体架构图：Application、Window/Input、TerrainRenderer、ImGuiLayer、ITerrainLodAlgorithm、Benchmark；
- 模块划分：输入模块、平台窗口模块、渲染模块、GUI 模块、算法模块、性能统计模块；
- 关键代码片段解析：
  - `ITerrainLodAlgorithm` 如何统一三种算法；
  - Classic ROAM 如何维护 pointer-based bintree 和 diamond split / merge；
  - Data-Oriented ROAM 如何用 index pool / SoA / thread-local buffer 降低 CPU 成本；
  - GPU ROAM-like 如何组织 node SSBO、active leaf compaction、compute pass、mesh emit 和 indirect draw；
  - Runtime benchmark 如何保证三算法使用同一条相机路径和同一组参数；
- 问题解决：结合 bug fix log 说明典型问题、定位过程和修复方案，例如 T-junction、三角绕序、持久拓扑不继续细分、GPU 长三角形、GPU 同步读回导致整体变慢。

#### 第四章 测试与结果分析

- 运行截图：不同视角、线框、LOD debug color、不同 height map 或参数；
- 性能分析：FPS、Frame ms、LOD total ms、CPU update、CPU upload、GPU compute、Draw Call、三角形数量；
- 算法效果对比：规则网格 vs Classic ROAM vs DOD ROAM vs GPU ROAM-like；
- 性能结果解释：
  - DOD 通常整体最快，说明 SoA 和并行 candidate marking 有收益；
  - GPU compute 本身快，但当前 GPU 版仍受 CPU DOD topology baseline、snapshot build、upload 和 readback wait 影响；
  - 该结论可以作为“GPU 算得快不等于系统一定快”的实验分析重点；
- 稳定性测试：smoke test、roam probe、runtime benchmark、GPU capability skip 语义。

#### 第五章 总结与展望

- 已完成工作：可运行地形渲染系统、三套 LOD 算法、调试面板、自动 benchmark、CSV / Markdown 报告；
- 存在不足：GPU 版仍非完整持久 GPU topology，CPU-GPU 数据交界成本较高，debug draw 和报告图表仍需整理；
- 未来优化方向：GPU 持久拓扑、dirty range / persistent mapped buffer、Mesh Shader、Geometry Clipmap / CBT、大世界 streaming、更多 terrain 数据集。

#### 参考文献

- ROAM 原始论文和 terrain LOD 相关资料；
- OpenGL / GLSL 官方文档；
- SDL2、Dear ImGui、GLM、stb、GLAD 等第三方库文档和 license；
- 若引用开源项目或博客，需要列出链接、作者和使用范围。

### 评分标准对齐

| 评分项 | 分值 | 本项目对应材料 |
|---|---:|---|
| 功能完整性 | 20 | 可运行程序、Height Map 地形、相机、GUI、三算法切换、benchmark、GPU capability gate |
| 技术难度 | 30 | Classic ROAM 拓扑、DOD SoA 与多线程、OpenGL Compute Shader、SSBO、active leaf compaction、indirect draw |
| 代码质量 | 15 | 分层架构、统一算法接口、命名规范、必要注释、bug log 和开发规范 |
| 技术报告 | 25 | 理论推导、架构图、核心代码解析、性能表格、截图、问题解决记录 |
| 创新性与表现力 | 10 | 三架构横向对比、运行时 benchmark、LOD debug view、GPU ROAM-like 实验路径 |

### 扣分项避坑

- 不提交网上完整源码冒充原创，第三方库和参考项目必须说明来源；
- 不遗漏可执行文件或运行说明；
- 不提交无法运行的包，如果目标机器缺 GPU compute，需要在说明中写清楚 fallback / skip 逻辑；
- 报告不能雷同或抄袭，项目结论必须基于自己的截图、CSV 和 bug fix log；
- 打包前清理编译中间文件和本地 IDE 缓存，只保留必要工程、资源、文档和演示程序。

### 推荐工具与开发环境

- 底层 API：OpenGL 4.3+；
- 语言与工程：C++20、CMake、MSVC / Visual Studio；
- 数学库：GLM；
- GUI 与窗口：SDL2、Dear ImGui；
- 资源与加载：stb、height map、PPM / PNG texture；
- Shader 调试：RenderDoc、OpenGL debug output、GLSL compiler log；
- 报告图表：runtime benchmark CSV、Markdown 汇总表、表格软件或 Python / Excel 绘图。

### 特别提示

- 不要把报告写成“功能流水账”，重点解释为什么这样设计、遇到什么问题、数据说明了什么；
- 图形学 debug 耗时很高，提交前应保留至少一套稳定的演示参数；
- 任何性能结论都要写明构建类型、GPU 型号、OpenGL 版本、VSync 状态、height map 和核心 LOD 参数；
- 如果 GPU 结果没有超过 DOD，不必回避；这正好能体现对 CPU-GPU 数据传输、同步和动态拓扑瓶颈的理解。

## 技术报告结构

### 第一章 绪论

- 实时地形渲染的 LOD 问题；
- ROAM 的背景与意义；
- 现代多核 CPU 与 GPU 并行架构下的挑战；
- 本项目工作与贡献。

### 第二章 相关理论与技术基础

- Height Map 地形表示；
- 三角网格与二叉三角树；
- ROAM split / merge；
- 邻接关系、diamond split 和裂缝；
- 几何误差与屏幕空间误差；
- 数据导向设计、SoA、任务并行；
- Compute Shader、SSBO、GPU-driven rendering。

### 第三章 系统设计与实现

- 总体架构图；
- SDL2 + CMake 初始框架；
- Classic ROAM；
- Data-Oriented CPU ROAM；
- GPU ROAM-like；
- UI、Debug View、性能统计；
- 关键代码与问题解决。

### 第四章 测试与结果分析

- 测试环境；
- 各类 Height Map；
- 视觉效果；
- 三版本性能对比；
- Data-Oriented 线程扩展性；
- 问题与限制；
- 失败案例分析。

### 第五章 总结与展望

- 已完成工作；
- 三种架构的适用性结论；
- 局限；
- 后续方向：GPU 完整拓扑更新、Mesh Shader、Geometry Clipmap / CBT、大世界 streaming，以及将 screen-space LOD 思想扩展到其他场景表示。

## 完成定义

项目完成时至少应具备：

- 一个可运行程序；
- 三种模式中至少两个可完整演示，GPU 模式至少有 Compute Shader 相关成果；
- 3 组以上可复现实验；
- CSV 数据、截图、性能曲线；
- 报告中能对应解释每个图表的意义；
- 对未完成的冲刺目标有清楚的技术原因说明。

