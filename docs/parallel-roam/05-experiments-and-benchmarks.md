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

> ROAM 口径更新（2026-08-03）：Classic 与 DOD 共用论文公式 (1) 的 nested wedgie thickness tree，预计算深度覆盖 height map 源分辨率并限制到 20；GPU ROAM-like 从 DOD snapshot 读取同一 `GeometricError`。三者还共享像素 SSE、六平面视锥感知和活动 leaf 硬预算。GPU 路径的持久拓扑仍由 CPU DOD 更新；原生 compute 只在快照上追加一轮 split-only，GPU merge candidate 尚不提交，GPU 新 child 也不写回下一帧 CPU 真值。因此内部 world-error 口径已经对齐，但 screen projection 仍是中心深度近似，跨架构质量结论仍应使用最终网格的公共离线指标。

> 数据可比性：公式 (1) 替换了旧的 `max(local,left,right)` 四点采样误差。2026-08-03 之前生成的 runtime 报告仍可描述当时的 CPU/GPU 阶段成本，但其三角形数量、score 分布和画质不能与新实现直接横向比较。需要用当前二进制重跑正式性能表。

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

三种 ROAM 的 `TriangleBudget` 都是最终活动 leaf 硬上限。Classic/DOD 的 requested、forced 和并行 chunk split 消费同一预算；当池接近满载且视点变化产生更高分 split 时，两者会用有界批次把低损失 diamond 的预算交换给新候选，避免新入屏区域等待旧视野完全越过 merge 阈值。GPU ROAM-like 先运行该 CPU DOD baseline，再由 compute counter 原子分配剩余 token：外边界单 split 消费 1，diamond pair 消费 2，提交失败时归还。原生 GPU pass 仍按候选 append 顺序竞争 token，不是全局误差 Top-K；固定预算能直接约束资源上限，但不代表三种实现做出了相同的全局预算分配。

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

该表分析稳定帧热路径。nested wedgie tree / `GeometricError` rebuild 属于初始化、地形切换或预计算深度失效后的 reset 路径，当前没有独立阶段计时；不能从稳定帧表中推断其成本，若要比较必须另建 initialization benchmark。

统一 benchmark harness 对 Classic、DOD 和 GPU 名称都应用预算与 `center -> away` 视锥回收断言；`budget-reentry` profile 另以低预算和原地小角度转向，要求 Classic/DOD 在转向后的第一次 Build 同时 merge 旧低分 diamond 并 split 新高分区域。无窗口模式因没有图形上下文通常跳过 GPU。应用级 `--gpu-smoke-test` 在 OpenGL 和 D3D12 上分别验证 GPU packet 非空、最终三角形不超预算，并检查 CPU DOD 持久拓扑的三类 issue 为零。这类正确性验证不替代 30-60 秒 runtime 性能采样。

### DOD active internal 索引 A/B

2026-08-01 的隔离测试使用 OpenGL RelWithDebInfo、Test129、Terrain size 30、Height scale 4、Max depth 14、split/merge 4 px / 2 px、Triangle budget 20000，每种算法运行 10 秒。Control 保留 `ActiveInternalNodes` 的维护成本，但让 merge candidate 回到完整持久 node pool 扫描；实验组只改为扫描 active internal 连续索引，因此差值主要反映候选来源变化。

| DOD 指标 | 完整 node pool control | Active internal index | 变化 |
| --- | ---: | ---: | ---: |
| Merge candidate mark | 1.6273 ms | 0.9777 ms | -39.9% |
| Merge pass | 1.6495 ms | 0.9985 ms | -39.5% |
| CPU update | 6.3595 ms | 5.3143 ms | -16.4% |
| Avg triangles | 12354.1 | 12434.6 | +0.7% |
| Avg nodes | 42756.9 | 42420.0 | -0.8% |
| Max topology issues | 0 | 0 | 不变 |

实验组为 `runtime-benchmark-20260801-030432`，control 为 `runtime-benchmark-20260801-030623`。两次运行的帧采样点数量随性能变化而不同，因此 triangles/nodes 不要求逐帧完全相等；当前结果证明候选扫描成本显著下降且输出规模没有系统性缩水，但正式报告仍应补充多轮重复和方差。

### DOD active leaf 融合 Split 扫描 A/B

本轮继续使用相同的 OpenGL RelWithDebInfo、Test129、Terrain size 30、Height scale 4、Max depth 14、split/merge 4 px / 2 px、Triangle budget 20000，每种算法运行 10 秒。基线为上述 active internal 优化后的 `runtime-benchmark-20260801-030432`；最终实验组为 `runtime-benchmark-20260801-195558`。

实现将 split 前的 active leaf 收集、预算 leaf 计数、像素 SSE/视锥评估、`ScreenErrors` 写入和 threshold 候选标记融合到 `CollectSplitCandidates` 的一次 active-leaf-index 扫描。预算满载时的 merge/split 重平衡仍需在正式 Split 之前评估需求，但也直接读取 `ActiveLeafNodes`，不再递归收集。DOD 的 `Budget leaf collect` 与独立 `Error eval` 兼容字段因此为 0，融合物理区间完整归入 `Split scan/mark`，避免同一循环被多个统计字段重复计时。

| DOD 指标 | 独立 pass 基线 | 融合 active-leaf-index 扫描 | 变化 |
| --- | ---: | ---: | ---: |
| Split 前扫描工作合计 | 1.6152 ms | 0.2969 ms | -81.6% |
| Split pass 包络 | 1.3832 ms | 0.3044 ms | -78.0% |
| CPU update | 5.3143 ms | 3.6372 ms | -31.6% |
| LOD total | 5.6192 ms | 3.8987 ms | -30.6% |
| Avg triangles | 12434.6 | 12283.3 | -1.2% |
| Avg nodes | 42420.0 | 42975.4 | +1.3% |
| Max topology issues | 0 | 0 | 不变 |

第一行基线是 `Budget leaf collect + Error eval + Split mark`；它包含基线中位于 Split pass 包络外的预算计数，所以不能与第二行混加。一次机械融合 control `runtime-benchmark-20260801-194942` 仍扫描完整持久 node pool，并为每个节点沿 parent 链判断是否 active，结果为 `1.6692 ms`，比基线三个阶段之和高 3.3%。最终收益因此应归因于增量维护 `ActiveLeafNodes`、只扫描真实活动叶以及消除重复遍历，而不是函数合并本身。

两组测试按固定运行时长而不是固定 Build 次数采样；优化后每秒 Build 次数增加，使相机路径上的采样密度和每帧 split/merge 数发生变化。triangles/nodes 的小幅差异不能直接解释为质量变化。本表是单轮方向性 A/B，正式论文或结项报告仍需固定轨迹、重复多轮并报告方差。

### DOD 最终 Active Leaf 输出视图复用

`ActiveLeafNodes` 已由每次 split/merge 增量维护，但旧输出路径仍在拓扑稳定后调用 `CollectLeafNodes`，从两个 root 递归遍历活动树并生成 `FinalActiveLeaves`。修复后 CPU mesh emit、统计和 GPU snapshot 直接只读复用 `ActiveLeafNodes`；`FinalActiveLeaves` 容器已删除。validator 仍从 root 独立递归收集，用不同真值来源校验活动索引和反向 position，避免索引自证正确。

OpenGL RelWithDebInfo 使用 Test129、Terrain size 30、Height scale 4、Max depth 14、split/merge 4 px / 2 px、Triangle budget 20000，各算法运行 10 秒。基线为 `runtime-benchmark-20260801-195558`，最终实验为 `runtime-benchmark-20260801-221506`。

| DOD 指标 | 递归最终收集 | 直接复用输出视图 | 变化 |
| --- | ---: | ---: | ---: |
| Final leaf collect/view | 0.2383 ms | 0.0000 ms | 独立 pass 消除 |
| Avg triangles | 12284.50 | 12289.76 | +0.04% |
| Avg nodes | 42971.53 | 42993.89 | +0.05% |
| Max topology issues | 0 | 0 | 不变 |

本次结构性收益可以由计时点直接确认，但单轮 `CPU update` 从 `3.6442 ms` 波动到 `3.7717 ms`，不能据此宣称整帧必然提速；两次固定时长运行的采样密度以及 merge/split 工作量并不逐帧配对。最终零复制实现另通过 DOD standard 64 帧验证，活动三角形范围保持 `12906..20000`，拓扑测试 6/6 通过。

### DOD Chunk 并行提交阈值验证

2026-08-01 使用严格配对实验验证了“候选数量达到多少时 chunk 并行提交开始获益”。每个目标 Build 之前都强制串行，只在目标 Build 的 Split 或 Merge 单个阶段切换 serial/parallel；每点运行 12 对并交替执行顺序。1,224 个目标帧样本的 candidates、non-empty chunks、triangles 和 split/merge count 均逐对一致，拓扑错误为 0，所有并行样本的成功 commit 数等于输入 candidate 数。

- Merge `<100` candidates 时总体不值得并行；`100-149` 属于噪声/不稳定区；`150-159` 开始过渡；`160-199` 的配对中位节省为 `0.0432 ms`、并行胜率 `79.8%`；`200-399` 中位节省 `0.0928 ms`、胜率 `86.7%`。保守的一维交叉点取 `160`。
- Split 在现有 Standard/Smoke/Budget-reentry 中最多只有 17 个安全 interior candidates。`2-17` 区间并行中位数反而慢 `0.0180 ms`，并行胜率 `37.5%`；当前证据支持保持串行，不能把 Merge 的 160 直接套给 Split。
- `nonEmptyChunkCount` 同样重要：相同 candidate 数在不同 chunk 分布下可能一快一慢。160 只适用于当前测试 CPU、`8x8` chunk 和最多 8 workers。
- 验证完成后，产品默认阈值已拆分为 Split 32、Merge 160；两者不再共用同一个阈值。完整方法、区间表、全轨迹确认和限制见 `benchmark-output/chunk-parallel-threshold-report-20260801.md`。

## 结论写法

所有项目自有的 `.md` 实验报告和运行时报告必须使用中文编写，包括标题、正文说明、状态、限制和结论。算法名、API 名、shader/pass 名、单位、CSV 字段和表格参数等技术术语可以保留英语；不得因此把整段说明或完整标题写成英语。报告生成器也必须遵守这一规则，保证新生成文件不需要再人工翻译。

报告中的结论可以采用如下框架：

> 经典 ROAM 在 CPU 上具有清晰的局部自适应拓扑维护能力；数据导向重构能显著改善可并行阶段的 CPU 利用率；GPU 化进一步减少了大规模误差评估和 LOD 决策的 CPU 负担，但不规则拓扑更新与邻接一致性仍是其主要限制。不同实现不存在绝对优劣，适用性取决于场景规模、拓扑变化频率、质量目标和工程复杂度约束。

具体数值必须由实验填充，不要提前写死。
