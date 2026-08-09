# 运行时基准测试报告

- 相机路径：从地形边缘中点移动到地形中心
- 每种算法的采样时长：10 秒
- 详细 CSV：`runtime-benchmark-20260809-024900.csv`

- 构建配置：RelWithDebInfo
- 图形后端：OpenGL
- 图形适配器：NVIDIA GeForce RTX 5090 D/PCIe/SSE2 (4.3.0 NVIDIA 591.86)
- Benchmark 标签：DOD serial split corrected topology accounting
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
| Classic CPU ROAM | 38 | 270.22 | 834.62 | 264.21 | 822.87 | 200000.00 | 200000 | 1165029.89 | 1560086 | 99.86 | 105.35 | 1 | 20 | 20 | 0 |
| Data-Oriented CPU ROAM | 44 | 252.97 | 1050.38 | 227.08 | 745.99 | 199999.41 | 200000 | 1153042.86 | 1581786 | 179.50 | 259.31 | 8 | 20 | 20 | 0 |
| GPU ROAM-like | 25 | 436.69 | 912.64 | 417.04 | 731.21 | 199999.60 | 200000 | 1052243.20 | 1515412 | 125.69 | 180.83 | 8 | 20 | 20 | 0 |

## ROAM 逻辑阶段对比

表中数值均为平均毫秒数。GPU ROAM-like 是混合路径：先运行完整的 CPU DOD 拓扑基线，再追加一轮 GPU split-only 细分和 GPU mesh emit。两个 GPU-like 列分别展示 CPU 与 shader 的重复职责，避免把 shader 链误解为完整的 GPU ROAM 实现。

| ROAM 逻辑阶段 | Classic CPU | DOD CPU | GPU-like CPU baseline | GPU-like shader | 阶段映射与限制 |
| --- | ---: | ---: | ---: | ---: | --- |
| Prepare / 帧状态 | 6.4416 | 6.2411 | 11.5808 | N/A | CPU 准备持久拓扑和 snapshot 输入 |
| Merge 候选评分 | 14.6752 | 3.3459 | 3.5326 | 0.9214 | Shader 为诊断重新扫描已 split 的父节点；GPU 不提交这些候选 |
| Merge 拓扑提交 / 向上级联 | 30.9758 | 52.3952 | 81.6249 | N/A | 持久 merge、邻接修复和级联回收由 Classic/DOD CPU 路径执行；GPU shader 未提交 merge |
| Split 前 active leaf 收集 | 0.0000 | 0.0000 | 0.0000 | 0.8800 | DOD CPU 直接以持久 Q_s 表示 active leaf 与预算计数，本列为 0；GPU 在评分前仍单独压缩 snapshot |
| 视点相关 leaf error / 视锥测试 | 0.0000 | 0.0000 | 0.0000 | 0.1738 | Classic 与 DOD 都把评分计入持久 Q_s 优先级刷新，本列为 0；GPU 基于 snapshot 单独评分 |
| Split 扫描/标记 | 43.2309 | 14.4754 | 17.9419 | 0.0743 | DOD CPU 数值包含持久 Q_s 的并行 SSE/视锥优先级刷新、原地建堆和提交快照生成；GPU append 顺序不是误差优先级顺序 |
| Split 拓扑 / 裂缝约束提交 | 64.1529 | 94.2948 | 227.9958 | 0.1629 | GPU 只提交一轮受预算约束的细分，并只处理直接 base-neighbor diamond pair；没有递归 forced-split 链 |
| 细分后 active leaf 收集 / 输出视图 | 0.0000 | 0.0000 | 0.0000 | 0.7711 | Classic 直接复用 dense mesh slot owners，DOD 直接复用 ActiveLeafNodes，因此两者本列均为 0；GPU 数值包含 counter reset 和 split 后 leaf compaction |
| Mesh emit / draw argument 生成 | 43.0603 | 17.6690 | 0.0000 | 0.5047 | Classic 只重写拓扑变化影响的稳定槽位；DOD 仍完整 emit；GPU 输出非共享顶点、索引和 indirect draw argument |
| Finalize / 发布 packet | 51.0508 | 33.8543 | 34.1470 | N/A | CPU 发布统计和 renderer 资源契约 |

## CPU 实现阶段

`CPU update` 包含下表中互斥的物理执行区间；`CPU upload` 是算法返回后的 renderer 上传。Classic 与 DOD 都持久维护 Q_s/Q_m，并在每个 Build 刷新现有成员的优先级。DOD 对两队列的评分并行化，`Split scan/mark` 包含 Q_s 优先级刷新、原地建堆和提交快照生成，`Merge mark` 对应 Q_m 的同类工作。DOD 满预算时持续执行 merge-first 资源交换，直到 max(Q_s) 不再高于 min(Q_m)。两者单独的 `Error eval` 都为零；Classic 的 `Mesh emit` 是 dirty-slot 增量更新，不再等同于完整网格构建；GPU shader 仍保留独立 dispatch。

| Algorithm | CPU update | Prepare | Merge mark | Merge topology | Budget leaf collect | Error eval | Split scan/mark | Split topology | Final leaf collect/view | Mesh emit | Finalize | CPU upload |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| Classic CPU ROAM | 254.5093 | 6.4416 | 14.6752 | 30.9758 | 0.0000 | 0.0000 | 43.2309 | 64.1529 | 0.0000 | 43.0603 | 51.0508 | 9.4854 |
| Data-Oriented CPU ROAM | 222.2763 | 6.2411 | 3.3459 | 52.3952 | 0.0000 | 0.0000 | 14.4754 | 94.2948 | 0.0000 | 17.6690 | 33.8543 | 4.0742 |
| GPU ROAM-like | 324.1724 | 11.5808 | 3.5326 | 81.6249 | 0.0000 | 0.0000 | 17.9419 | 227.9958 | 0.0000 | 0.0000 | 34.1470 | 13.4186 |

### DOD 拓扑六段计时

六段均为互斥执行区间。`六段合计` 与原有 `Topology total` 的差值是函数调用、worker 数决策和少量循环控制等尚未单列的开销。

| 操作 | 候选排序/分桶 | 队列邻域失效 | chunk 并行提交 | worker 结果汇总 | 活动叶索引/队列刷新 | 串行收敛 | 六段合计 | Topology total |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| Split | 0.0000 | 0.0000 | 0.0000 | 0.0000 | 0.0000 | 94.2942 | 94.2942 | 94.2948 |
| Merge | 0.6591 | 1.7754 | 0.9280 | 0.0135 | 7.2663 | 41.7335 | 52.3758 | 52.3952 |

### 增量 CPU Mesh 输出

`Full rebuilds` 是采样窗口内的全量初始化次数；其余列是逐帧平均值。当前只有 Classic 填充这些字段，其他算法为零不代表它们也采用了增量输出。D3D12 的 frame slot 会延迟消费两次使用之间积累的 dirty range 并集，因此 `Max upload bytes` 表示实际补齐量，不要求等于当前 Build 的 updated triangles。

| Algorithm | Full rebuilds | Updated triangles | Reused triangles | Dirty ranges | Max upload bytes |
| --- | ---: | ---: | ---: | ---: | ---: |
| Classic CPU ROAM | 1 | 100379.6053 | 99620.3947 | 27135.5526 | 33600000 |
| Data-Oriented CPU ROAM | 0 | 0.0000 | 0.0000 | 0.0000 | 33600000 |
| GPU ROAM-like | 0 | 0.0000 | 0.0000 | 0.0000 | 169726196 |

### 原生 pass 包络

这些是各实现原有的外围 pass 计时。它们与上方互斥阶段重叠，不能重复相加。

| Algorithm | Split | Merge | Emit | Validate |
| --- | ---: | ---: | ---: | ---: |
| Classic CPU ROAM | 107.3838 | 45.6510 | 43.0603 | 0.0000 |
| Data-Oriented CPU ROAM | 141.4493 | 55.7411 | 17.6690 | 0.0000 |
| GPU ROAM-like | 245.9377 | 85.1575 | 0.0000 | 0.0000 |

## GPU shader dispatch 明细

每个数值都是一个物理 dispatch 的延迟 GPU timestamp 结果。`Pass sum` 是八个互不重叠区间之和。本表用于解释上方 GPU-like shader 列，不能据此认定所有 ROAM 阶段都已在 GPU 实现。`Dispatch wall`、query wait、readback wait 和 frame-fence wait 都是 CPU 侧开销，不计入 `Pass sum`。

| Algorithm | Pre-split leaf collect | Leaf error / frustum | Split candidate mark | Merge candidate score | Split / direct-diamond commit | Leaf-counter reset | Post-refine leaf collect | Mesh emit / draw args | Pass sum | Max pass sum |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| GPU ROAM-like | 0.8800 | 0.1738 | 0.0743 | 0.9214 | 0.1629 | 0.0075 | 0.7636 | 0.5047 | 3.4882 | 8.7420 |

## GPU 编排与渲染

| Algorithm | Snapshot build | Buffer allocation | Dispatch wall | Query wait | Readback wait | Frame fence wait | Render | Max render | Max upload B | Max readback B |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| Classic CPU ROAM | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 33600000 | 0 |
| Data-Oriented CPU ROAM | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 33600000 | 0 |
| GPU ROAM-like | 28.02 | 0.00 | 0.60 | 0.00 | 45.43 | 0.00 | 0.00 | 0.00 | 169726196 | 96 |
