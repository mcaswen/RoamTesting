# 实验与基准测试

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

当前 Classic 已输出 Original/Subdivided/Rebuilt/depth 颜色和 forced split 高亮，并可显示拓扑验证统计；score heatmap、diamond pair overlay 和 chunk 边界仍是建议项，不应写成已完成能力。

## 主实验：三版本对比

> 当前边界：本实验继续用于 Classic CPU、Data-Oriented CPU 和 GPU ROAM-like 的成熟路径对比。CBT 2024 在完整 split/merge、高度图几何和统计接入完成前只运行专用正确性验证，不进入本表性能排名。

> ROAM 口径更新（2026-07-29）：Classic、DOD 和 GPU ROAM-like 共享完整方差、像素 SSE、六平面视锥感知和活动 leaf 硬预算。GPU 路径的持久拓扑仍由 CPU DOD 更新；原生 compute 只在快照上追加一轮 split-only，GPU merge candidate 尚不提交，GPU 新 child 也不写回下一帧 CPU 真值。因此内部评分单位已经对齐，但跨架构质量结论仍应使用最终网格的公共离线指标。

统一控制变量：

```text
- 同一 Height Map
- 同一 terrain size
- 同一 max depth
- 三种 ROAM 使用同一像素 split/merge 阈值；其他算法记录自己的评分器与单位
- 同一相机路径
- 同一渲染分辨率
- 同一活动三角形预算或离线质量目标
```

三种 ROAM 的 `TriangleBudget` 都是最终活动 leaf 硬上限。Classic/DOD 的 requested、forced 和并行 chunk split 消费同一预算；GPU ROAM-like 先扣除 CPU DOD 快照已占用的 leaf，再由 compute counter 原子分配剩余 token：外边界单 split 消费 1，diamond pair 消费 2，提交失败时归还。该 GPU pass 按候选 append 顺序竞争 token，不是全局误差 Top-K；固定预算能直接约束资源上限，但不代表三种实现做出了相同的全局预算分配。

主要对比指标：

| 指标 | Classic CPU | Data-Oriented CPU | GPU ROAM-like |
|---|---:|---:|---:|
| CPU Prepare |  |  |  |
| CPU Merge Candidate Mark |  |  |  |
| CPU Merge Topology |  |  |  |
| CPU Budget Leaf Collect |  |  |  |
| CPU Error Evaluation |  |  |  |
| CPU Split Candidate Mark |  |  |  |
| CPU Split Topology |  |  |  |
| CPU Final Leaf Collect |  |  |  |
| CPU Mesh Emit |  |  |  |
| CPU Finalize |  |  |  |
| CPU Update (envelope) |  |  |  |
| Native Split/Merge/Emit/Validate (overlapping envelopes) |  |  |  |
| CPU Worker Count |  |  |  |
| CPU Utilization |  |  |  |
| GPU Initial Leaf Compaction |  |  |  |
| GPU Error Evaluation |  |  |  |
| GPU Split Candidate Marking |  |  |  |
| GPU Merge Candidate Scoring |  |  |  |
| GPU Split / Direct-Diamond Commit |  |  |  |
| GPU Leaf-Collect Counter Reset |  |  |  |
| GPU Post-Refine Leaf Collection |  |  |  |
| GPU Mesh Emit / Draw Args |  |  |  |
| GPU Pass Sum |  |  |  |
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
screenSpaceSplitThresholdPixels
screenSpaceMergeThresholdPixels
triangleBudget
budgetRejectedSplitCount
verticalFovDegrees
drawableWidth
drawableHeight
workerThreadCount
cpuUtilizationPercent
activeTriangleCount
activeNodeCount
splitCount
mergeCount
maxActiveDepth
averageActiveDepth
cpuPrepareMs
cpuMergeCandidateMarkMs
cpuMergeTopologyMs
cpuBudgetLeafCollectMs
cpuErrorEvalMs
cpuSplitCandidateMarkMs
cpuSplitTopologyMs
cpuFinalLeafCollectMs
cpuMeshEmitMs
cpuFinalizeMs
cpuUploadMs
gpuInitialLeafCompactionMs
gpuErrorEvaluationMs
gpuSplitCandidateMarkingMs
gpuMergeCandidateMarkingMs
gpuSplitTopologyMs
gpuActiveLeafResetMs
gpuFinalLeafCompactionMs
gpuMeshEmitMs
gpuPassSumMs
gpuSnapshotBuildMs
gpuBufferAllocationMs
gpuDispatchWallMs
gpuQueryWaitMs
gpuReadbackWaitMs
renderMs
totalFrameMs
fps
cpuGpuUploadBytes
cpuGpuReadbackBytes
```

`cpuUtilizationPercent` 使用进程 CPU time / build wall time 的口径，单个逻辑核心满载约为 100%，多线程算法可以超过 100%。

CPU 阶段按互斥执行区间记录，阶段和应接近 `cpuUpdateMs`；原生 `split/merge/emit/validate` 仍作为重叠的 pass 包络保留，不能与互斥阶段重复相加。GPU 不再只报告设备总时间：OpenGL 使用八个顺序 `GL_TIME_ELAPSED` query，D3D12 使用九个 timestamp 边界，分别得到 split 前活动叶收集、活动叶像素误差/视锥测试、split 候选标记、merge parent 评分、split/direct-diamond 提交、活动叶计数重置、split 后活动叶收集和 mesh/indirect-args 输出。`gpuPassSumMs` 只是八段之和，不含 CPU dispatch wall、query/readback wait 或 render。

运行时 Markdown 的首要比较表必须以 ROAM 逻辑阶段为行，而不是以 shader 名为行。GPU ROAM-like 是混合实现：完整 merge、级联回收、邻接修复和 CPU split baseline 仍由 DOD 执行；GPU merge shader 当前只产生诊断候选，GPU 拓扑只追加一轮 split，并仅支持直接 base-neighbor diamond。报告必须分别显示 `GPU-like CPU baseline` 与 `GPU-like shader`，未实现的 GPU merge topology 写成 `N/A`，不能用 `0 ms` 暗示它已经实现。

该表分析稳定帧热路径。variance tree / geometric-error rebuild 属于初始化、地形切换或相关设置失效后的 reset 路径，当前没有独立阶段计时；不能从稳定帧表中推断其成本，若要比较必须另建 initialization benchmark。

统一 benchmark harness 对 Classic、DOD 和 GPU 名称都应用预算与 `center -> away` 视锥回收断言；无窗口模式因没有图形上下文通常跳过 GPU。应用级 `--gpu-smoke-test` 在 OpenGL 和 D3D12 上分别验证 GPU packet 非空、最终三角形不超预算，并检查 CPU DOD 持久拓扑的三类 issue 为零。这类正确性验证不替代 30-60 秒 runtime 性能采样。

## 结论写法

所有项目自有的 `.md` 实验报告和运行时报告必须使用中文编写，包括标题、正文说明、状态、限制和结论。算法名、API 名、shader/pass 名、单位、CSV 字段和表格参数等技术术语可以保留英语；不得因此把整段说明或完整标题写成英语。报告生成器也必须遵守这一规则，保证新生成文件不需要再人工翻译。

报告中的结论可以采用如下框架：

> 经典 ROAM 在 CPU 上具有清晰的局部自适应拓扑维护能力；数据导向重构能显著改善可并行阶段的 CPU 利用率；GPU 化进一步减少了大规模误差评估和 LOD 决策的 CPU 负担，但不规则拓扑更新与邻接一致性仍是其主要限制。不同实现不存在绝对优劣，适用性取决于场景规模、拓扑变化频率、质量目标和工程复杂度约束。

具体数值必须由实验填充，不要提前写死。
