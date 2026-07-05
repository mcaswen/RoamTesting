# Project Scope and Goals

## 项目定位

本项目实现一个基于 Height Map 的实时自适应地形渲染系统，并围绕同一套 ROAM（Real-time Optimally Adapting Meshes）思想，比较三种计算架构下的实现方式：

1. Classic CPU ROAM：传统对象/节点树方式，作为正确性和性能基准。
2. Data-Oriented CPU ROAM：数组化、SoA、索引邻接关系和多线程任务划分。
3. GPU ROAM-like：将适合并行的误差评估、LOD 决策、活动叶节点收集和部分渲染准备迁移至 GPU Compute Shader。

研究问题是：同一个动态地形 LOD 问题，在传统 CPU、数据导向多核 CPU 与 GPU 并行架构下，应该如何组织数据、拆分任务、处理拓扑依赖，并在性能与工程复杂度之间取得平衡。

## 问题背景

Height Map 地形可以看作二维定义域上的高度函数：

```text
y = H(x, z)
```

若按最高分辨率直接三角化整张地形，近处和远处都会使用大量三角形，造成不必要的渲染和 CPU/GPU 负担。

ROAM 的核心思想是：

- 从覆盖 Height Map 二维区域的两个根三角形开始；
- 根据地形几何误差和相机视角，动态决定哪些区域需要细分；
- 相机附近、地形崎岖的区域使用更多三角形；
- 远处、平坦区域保持较粗网格；
- 通过邻接关系和 forced split / diamond split 避免不同细分等级间的裂缝。

## 目标分层

### 基础目标

- 加载或程序化生成 Height Map；
- 使用 OpenGL 渲染三维地形；
- 支持 FPS 相机、基础光照和材质；
- 实现基础 UI：FPS、三角形数量、当前模式、阈值、线程数等；
- 实现 Classic CPU ROAM，并正确生成动态 LOD 地形。

### 对比目标

- 实现 Data-Oriented CPU ROAM；
- 将可并行阶段拆分为任务并使用线程池执行；
- 实现 GPU Compute 驱动的 ROAM-like 管线；
- 在相同 Height Map、相同误差阈值、相同相机路径下比较三种实现；
- 输出性能与视觉质量数据。

### 高分增强目标

- crack-free 邻接约束；
- split / merge hysteresis，减少 LOD 抖动；
- LOD depth heatmap；
- screen-space error heatmap；
- CPU/GPU 时间统计；
- indirect draw 或 GPU 端活动叶节点压缩；
- 多种地形复杂度、相机距离和误差阈值下的实验对比。

### 冲刺目标

- GPU split-only；
- GPU split/merge + crack-free dependency propagation；
- terrain chunk 并行；
- Mesh Shader 或其他更现代 GPU pipeline 的探索。

## 明确非目标

这些内容可以作为报告展望，但不应吞掉主线时间：

- 无限大 terrain streaming；
- 植被、河流、天气等复杂场景系统；
- PBR 全套材质；
- 完整 GPU priority queue；
- 多平台适配；
- 商业级地形编辑器。

所有额外视觉效果都应服务于地形 LOD 展示。

## 成功标准

一个合理的结课版本至少应该满足：

- 有一个可交互的 Height Map 地形场景；
- Classic CPU 版能展示近细远粗的 ROAM LOD；
- Data-Oriented CPU 版能给出可解释的阶段耗时变化；
- GPU 版至少完成 error evaluation 和 candidate marking 中的一项，最好两项都完成；
- 能用截图、CSV、曲线和固定相机路径支撑结论；
- 报告能够诚实说明三种架构各自的优势、限制和适用范围。

