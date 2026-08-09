# 运行时基准测试报告

- 相机路径：从地形边缘中点移动到地形中心
- 每种算法的采样时长：10 秒
- 详细 CSV：`runtime-benchmark-20260810-002118.csv`

- 构建配置：RelWithDebInfo
- 图形后端：OpenGL
- 图形适配器：NVIDIA GeForce RTX 5090 D/PCIe/SSE2 (4.3.0 NVIDIA 591.86)
- GPU 设备：NVIDIA GeForce RTX 5090 D/PCIe/SSE2 (4.3.0 NVIDIA 591.86)
- GPU 计时模型：先运行 CPU DOD 拓扑基线，再按顺序执行 shader 阶段：split 前 leaf 收集、leaf error 评估、split 候选标记、诊断性 merge 候选评分、split/direct-diamond 拓扑提交、细分后 leaf 收集和 mesh emit；GPU merge 拓扑尚未实现；indirect draw 单独统计
- Benchmark 路径：极限压力路径
- VSync：基准测试期间已关闭

- Height map：`assets/heightmaps/Hm_Terrain_Peking_513.png` 547x547
- Terrain size：80
- Height scale：12
- Max depth 设置：20
- ROAM 屏幕空间 split/merge 阈值：0.25 px / 0.1 px
- ROAM triangle budget：200000
- DOD 并行 Split：关闭

## 总体结果

| Algorithm | Samples | Avg Frame ms | Max Frame ms | Avg LOD ms | Max LOD ms | Avg Triangles | Max Triangles | Avg Nodes | Max Nodes | Avg CPU % | Max CPU % | Max Workers | Config Max Depth | Reached Max Depth | Max Topology Issues |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| Classic CPU ROAM | 42 | 243.26 | 713.96 | 236.94 | 702.69 | 200000.00 | 200000 | 1157043.48 | 1576472 | 99.38 | 111.37 | 1 | 20 | 20 | 0 |
| Data-Oriented CPU ROAM | 63 | 174.82 | 906.81 | 156.56 | 594.60 | 199999.46 | 200000 | 1160419.94 | 1609960 | 190.81 | 441.53 | 8 | 20 | 20 | 0 |
| GPU ROAM-like | 31 | 348.64 | 720.53 | 334.74 | 639.58 | 199999.52 | 200000 | 1070723.29 | 1544930 | 125.67 | 175.49 | 8 | 20 | 20 | 0 |

## ROAM 逻辑阶段对比

表中数值均为平均毫秒数。GPU ROAM-like 是混合路径：先运行完整的 CPU DOD 拓扑基线，再追加一轮 GPU split-only 细分和 GPU mesh emit。两个 GPU-like 列分别展示 CPU 与 shader 的重复职责，避免把 shader 链误解为完整的 GPU ROAM 实现。

| ROAM 逻辑阶段 | Classic CPU | DOD CPU | GPU-like CPU baseline | GPU-like shader | 阶段映射与限制 |
| --- | ---: | ---: | ---: | ---: | --- |
| Prepare / 帧状态 | 6.5116 | 6.8869 | 11.8507 | N/A | CPU 准备持久拓扑和 snapshot 输入 |
| Merge 候选评分 | 13.5750 | 2.8451 | 3.3202 | 0.0525 | Shader 为诊断重新扫描已 split 的父节点；GPU 不提交这些候选 |
| Merge 拓扑提交 / 向上级联 | 26.9886 | 31.9807 | 61.0479 | N/A | 持久 merge、邻接修复和级联回收由 Classic/DOD CPU 路径执行；GPU shader 未提交 merge |
| Split 前 active leaf 收集 | 0.0000 | 0.0000 | 0.0000 | 0.0557 | DOD CPU 直接以持久 Q_s 表示 active leaf 与预算计数，本列为 0；GPU 在评分前仍单独压缩 snapshot |
| 视点相关 leaf error / 视锥测试 | 0.0000 | 0.0000 | 0.0000 | 0.0173 | Classic 与 DOD 都把评分计入持久 Q_s 优先级刷新，本列为 0；GPU 基于 snapshot 单独评分 |
| Split 扫描/标记 | 41.3059 | 7.5003 | 11.2717 | 0.0113 | DOD CPU 数值包含持久 Q_s 的并行 SSE/视锥优先级刷新、原地建堆和提交快照生成；GPU append 顺序不是误差优先级顺序 |
| Split 拓扑 / 裂缝约束提交 | 55.8478 | 56.1576 | 168.2493 | 0.0185 | GPU 只提交一轮受预算约束的细分，并只处理直接 base-neighbor diamond pair；没有递归 forced-split 链 |
| 细分后 active leaf 收集 / 输出视图 | 0.0000 | 0.0000 | 0.0000 | 0.0476 | Classic 直接复用 dense mesh slot owners，DOD 直接复用 ActiveLeafNodes，因此两者本列均为 0；GPU 数值包含 counter reset 和 split 后 leaf compaction |
| Mesh emit / draw argument 生成 | 37.5742 | 16.8707 | 0.0000 | 0.0964 | Classic 只重写拓扑变化影响的稳定槽位；DOD 仍完整 emit；GPU 输出非共享顶点、索引和 indirect draw argument |
| Finalize / 发布 packet | 46.7862 | 29.5583 | 33.0179 | N/A | CPU 发布统计和 renderer 资源契约 |

## CPU 实现阶段

`CPU update` 包含下表中互斥的物理执行区间；`CPU upload` 是算法返回后的 renderer 上传。Classic 与 DOD 都持久维护 Q_s/Q_m，并在每个 Build 刷新现有成员的优先级。DOD 对两队列的评分并行化，`Split scan/mark` 包含 Q_s 优先级刷新、原地建堆和提交快照生成，`Merge mark` 对应 Q_m 的同类工作。DOD 满预算时持续执行 merge-first 资源交换，直到 max(Q_s) 不再高于 min(Q_m)。两者单独的 `Error eval` 都为零；Classic 的 `Mesh emit` 是 dirty-slot 增量更新，不再等同于完整网格构建；GPU shader 仍保留独立 dispatch。

| Algorithm | CPU update | Prepare | Merge mark | Merge topology | Budget leaf collect | Error eval | Split scan/mark | Split topology | Final leaf collect/view | Mesh emit | Finalize | CPU upload |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| Classic CPU ROAM | 228.5906 | 6.5116 | 13.5750 | 26.9886 | 0.0000 | 0.0000 | 41.3059 | 55.8478 | 0.0000 | 37.5742 | 46.7862 | 8.1564 |
| Data-Oriented CPU ROAM | 151.8001 | 6.8869 | 2.8451 | 31.9807 | 0.0000 | 0.0000 | 7.5003 | 56.1576 | 0.0000 | 16.8707 | 29.5583 | 3.9993 |
| GPU ROAM-like | 249.9917 | 11.8507 | 3.3202 | 61.0479 | 0.0000 | 0.0000 | 11.2717 | 168.2493 | 0.0000 | 0.0000 | 33.0179 | 15.3339 |

### CPU Split 拓扑阶段计时（统一口径）

六段均为互斥执行区间。Classic 不执行并行预提交，因此前五项为 0；它与 DOD 的`串行收敛` 都从候选刷新结束后开始，并扣除循环中执行的 merge。`六段合计` 与`Topology total` 的差值是函数调用、worker 数决策和少量循环控制等尚未单列的开销。

| 操作 | 候选排序/分桶 | 队列邻域失效 | chunk 并行提交 | worker 结果汇总 | 活动叶索引/队列刷新 | 串行收敛 | 六段合计 | Topology total |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| Classic Split | 0 | 0 | 0 | 0 | 0 | 55.8478 | 55.8478 | 55.8478 |
| DoD Split | 0.0000 | 0.0000 | 0.0000 | 0.0000 | 0.0000 | 56.1573 | 56.1573 | 56.1576 |
| Merge | 0.3640 | 1.1358 | 0.6355 | 0.0100 | 4.6943 | 25.1235 | 31.9631 | 31.9807 |

### 增量 CPU Mesh 输出

`Full rebuilds` 是采样窗口内的全量初始化次数；其余列是逐帧平均值。当前只有 Classic 填充这些字段，其他算法为零不代表它们也采用了增量输出。D3D12 的 frame slot 会延迟消费两次使用之间积累的 dirty range 并集，因此 `Max upload bytes` 表示实际补齐量，不要求等于当前 Build 的 updated triangles。

| Algorithm | Full rebuilds | Updated triangles | Reused triangles | Dirty ranges | Max upload bytes |
| --- | ---: | ---: | ---: | ---: | ---: |
| Classic CPU ROAM | 1 | 93742.5000 | 106257.5000 | 27271.0238 | 33600000 |
| Data-Oriented CPU ROAM | 0 | 0.0000 | 0.0000 | 0.0000 | 33600000 |
| GPU ROAM-like | 0 | 0.0000 | 0.0000 | 0.0000 | 173032212 |

### 原生 pass 包络

这些是各实现原有的外围 pass 计时。它们与上方互斥阶段重叠，不能重复相加。

| Algorithm | Split | Merge | Emit | Validate |
| --- | ---: | ---: | ---: | ---: |
| Classic CPU ROAM | 97.1538 | 40.5636 | 37.5742 | 0.0000 |
| Data-Oriented CPU ROAM | 83.6607 | 34.8258 | 16.8707 | 0.0000 |
| GPU ROAM-like | 179.5210 | 64.3680 | 0.0000 | 0.0000 |

## GPU shader dispatch 明细

每个数值都是一个物理 dispatch 的延迟 GPU timestamp 结果。`Pass sum` 是八个互不重叠区间之和。本表用于解释上方 GPU-like shader 列，不能据此认定所有 ROAM 阶段都已在 GPU 实现。`Dispatch wall`、query wait、readback wait 和 frame-fence wait 都是 CPU 侧开销，不计入 `Pass sum`。

| Algorithm | Pre-split leaf collect | Leaf error / frustum | Split candidate mark | Merge candidate score | Split / direct-diamond commit | Leaf-counter reset | Post-refine leaf collect | Mesh emit / draw args | Pass sum | Max pass sum |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| GPU ROAM-like | 0.0557 | 0.0173 | 0.0113 | 0.0525 | 0.0185 | 0.0044 | 0.0432 | 0.0964 | 0.2993 | 0.5413 |

## GPU 编排与渲染

| Algorithm | Snapshot build | Buffer allocation | Dispatch wall | Query wait | Readback wait | Frame fence wait | Render | Max render | Max upload B | Max readback B |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| Classic CPU ROAM | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 33600000 | 0 |
| Data-Oriented CPU ROAM | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 33600000 | 0 |
| GPU ROAM-like | 32.74 | 0.00 | 0.73 | 0.00 | 30.54 | 0.00 | 0.00 | 0.00 | 173032212 | 96 |
