# Experiments and Benchmarks

## 可验证假设

项目不应预设“GPU 一定全面最好”，而应测试以下假设。

### Classic CPU ROAM

- 算法逻辑最直观；
- 邻接和拓扑更新容易调试；
- 大规模场景下受单线程、指针跳转和 CPU mesh rebuild 限制。

### Data-Oriented CPU ROAM

- error evaluation、candidate marking、leaf collection 可以获得较高并行收益；
- SoA 和 index-based layout 改善 cache locality；
- topology commit 仍受邻接关系和 split/merge 依赖限制；
- 不一定所有阶段都能线性加速。

### GPU ROAM-like

- 大规模误差评估与候选标记应显著降低 CPU 开销；
- 复杂动态拓扑、邻接传播与 merge 是 GPU 化的主要困难；
- GPU 方案的工程复杂度和调试成本较高；
- 在大规模 terrain 或高节点数量下更可能体现优势。

## Debug View

建议实现：

- 实体地形；
- wireframe；
- LOD depth heatmap；
- geometric error / screen-space error heatmap；
- split candidate 高亮；
- forced split 高亮；
- chunk 边界；
- active triangle count；
- crack check view。

Debug View 的价值不只是展示效果，也用于定位算法问题。比如 forced split 高亮可以直接解释为什么某些远处三角形被连带细分。

## 主实验：三版本对比

统一控制变量：

```text
- 同一 Height Map
- 同一 terrain size
- 同一 max depth
- 同一 split/merge threshold
- 同一相机路径
- 同一渲染分辨率
- 同一质量目标
```

主要对比指标：

| 指标 | Classic CPU | Data-Oriented CPU | GPU ROAM-like |
|---|---:|---:|---:|
| CPU Error Evaluation |  |  |  |
| CPU LOD Decision |  |  |  |
| CPU Topology Update |  |  |  |
| CPU Collect / Mesh Build |  |  |  |
| GPU Compute |  |  |  |
| Render Time |  |  |  |
| Total Frame Time |  |  |  |
| FPS |  |  |  |
| Active Triangles |  |  |  |
| Memory Use |  |  |  |
| CPU-GPU Upload Size |  |  |  |
| CPU-GPU Readback Size |  |  |  |

## 辅助实验：线程扩展性

只用于解释 Data-Oriented 版的可扩展性：

```text
1 / 2 / 4 / 8 / 16 / 32 worker threads
```

建议测试：

- Error Evaluation；
- Candidate Marking；
- Active Leaf Collection；
- Topology Commit；
- Overall Update。

如果总耗时没有随线程数下降，也要保留结果并解释原因，例如 topology commit 占比过高、任务过细、false sharing 或内存带宽限制。

## 地形场景

- 平坦地形；
- 中等山地；
- 高频噪声地形；
- 峡谷/悬崖地形；
- 不同尺寸 Height Map。

建议至少选择 3 类地形：平坦、中等复杂、高频复杂。这样报告能解释“算法在什么情况下收益最大”。

## Benchmark 流程

推荐流程：

1. 启动后先 warm-up 3 到 5 秒，不记录数据；
2. 使用固定相机路径回放 30 到 60 秒；
3. 每帧记录统计数据；
4. 分别输出平均值、p50、p95、最大值；
5. 对 GPU 计时使用 OpenGL Timer Query；
6. 单独标注是否发生 CPU readback；
7. 每组实验保存配置参数、截图和 CSV。

## CSV 字段建议

```text
frameIndex
timeSeconds
mode
terrainName
heightMapSize
maxDepth
splitThreshold
mergeThreshold
workerThreadCount
activeTriangleCount
activeNodeCount
splitCount
mergeCount
maxActiveDepth
averageActiveDepth
cpuErrorEvalMs
cpuDecisionMs
cpuTopologyMs
cpuCollectMs
cpuMeshBuildMs
cpuUploadMs
gpuComputeMs
renderMs
totalFrameMs
fps
cpuGpuUploadBytes
cpuGpuReadbackBytes
```

## 结论写法

报告中的结论可以采用如下框架：

> 经典 ROAM 在 CPU 上具有清晰的局部自适应拓扑维护能力；数据导向重构能显著改善可并行阶段的 CPU 利用率；GPU 化进一步减少了大规模误差评估和 LOD 决策的 CPU 负担，但不规则拓扑更新与邻接一致性仍是其主要限制。不同实现不存在绝对优劣，适用性取决于场景规模、拓扑变化频率、质量目标和工程复杂度约束。

具体数值必须由实验填充，不要提前写死。

