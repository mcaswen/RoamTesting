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

