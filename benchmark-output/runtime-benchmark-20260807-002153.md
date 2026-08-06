# 运行时基准测试报告

- 相机路径：从地形边缘中点移动到地形中心
- 每种算法的采样时长：10 秒
- 详细 CSV：`runtime-benchmark-20260807-002153.csv`

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

## 总体结果

| Algorithm | Samples | Avg Frame ms | Max Frame ms | Avg LOD ms | Max LOD ms | Avg Triangles | Max Triangles | Avg Nodes | Max Nodes | Avg CPU % | Max CPU % | Max Workers | Config Max Depth | Reached Max Depth | Max Topology Issues |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| Classic CPU ROAM | 29 | 345.23 | 1635.07 | 325.48 | 756.48 | 200000.00 | 200000 | 1121748.21 | 1496980 | 100.34 | 104.63 | 1 | 20 | 20 | 0 |
| Data-Oriented CPU ROAM | 50 | 219.05 | 921.69 | 204.31 | 483.70 | 200000.00 | 200000 | 478663.96 | 508092 | 161.17 | 283.65 | 8 | 20 | 20 | 0 |
| GPU ROAM-like | 45 | 243.15 | 739.06 | 231.12 | 505.91 | 200000.00 | 200000 | 478406.71 | 507364 | 129.92 | 211.63 | 8 | 20 | 20 | 0 |

## ROAM 逻辑阶段对比

表中数值均为平均毫秒数。GPU ROAM-like 是混合路径：先运行完整的 CPU DOD 拓扑基线，再追加一轮 GPU split-only 细分和 GPU mesh emit。两个 GPU-like 列分别展示 CPU 与 shader 的重复职责，避免把 shader 链误解为完整的 GPU ROAM 实现。

| ROAM 逻辑阶段 | Classic CPU | DOD CPU | GPU-like CPU baseline | GPU-like shader | 阶段映射与限制 |
| --- | ---: | ---: | ---: | ---: | --- |
| Prepare / 帧状态 | 10.0669 | 9.2513 | 9.3653 | N/A | CPU 准备持久拓扑和 snapshot 输入 |
| Merge 候选评分 | 14.0653 | 40.2421 | 41.1680 | 0.0490 | Shader 为诊断重新扫描已 split 的父节点；GPU 不提交这些候选 |
| Merge 拓扑提交 / 向上级联 | 40.9936 | 1.3323 | 1.4296 | N/A | 持久 merge、邻接修复和级联回收只由 CPU DOD 执行 |
| Split 前 active leaf 收集 | 0.0000 | 0.0000 | 0.0000 | 0.0403 | DOD CPU 已将 active leaf 遍历与计数融合进 Split 候选扫描，本列为 0；GPU 在评分前仍单独压缩 snapshot |
| 视点相关 leaf error / 视锥测试 | 0.0000 | 0.0000 | 0.0000 | 0.0724 | Classic 与 DOD 都在 Split 扫描中内联评估，DOD 本列为 0；GPU 基于 snapshot 单独评分 |
| Split 扫描/标记 | 42.5828 | 7.2315 | 7.1818 | 0.0210 | DOD CPU 数值包含 active leaf 遍历、SSE/视锥评估和 threshold 标记；GPU append 顺序不是误差优先级顺序 |
| Split 拓扑 / 裂缝约束提交 | 81.5351 | 88.4044 | 90.2034 | 0.0291 | GPU 只提交一轮受预算约束的细分，并只处理直接 base-neighbor diamond pair；没有递归 forced-split 链 |
| 细分后 active leaf 收集 / 输出视图 | 0.0000 | 0.0000 | 0.0000 | 0.0294 | Classic 直接复用 dense mesh slot owners，DOD 直接复用 ActiveLeafNodes，因此两者本列均为 0；GPU 数值包含 counter reset 和 split 后 leaf compaction |
| Mesh emit / draw argument 生成 | 60.0972 | 14.2629 | 0.0000 | 0.8621 | Classic 只重写拓扑变化影响的稳定槽位；DOD 仍完整 emit；GPU 输出非共享顶点、索引和 indirect draw argument |
| Finalize / 发布 packet | 63.0222 | 38.3083 | 44.9063 | N/A | CPU 发布统计和 renderer 资源契约 |

## CPU 实现阶段

`CPU update` 包含下表中互斥的物理执行区间；`CPU upload` 是算法返回后的 renderer 上传。Classic 在扫描和弹出 split queue 时评估屏幕误差；DOD 已把 active leaf 遍历、SSE/视锥评估和 threshold 标记融合到同一次 active-leaf-index 扫描。因此两者单独的 `Error eval` 都为零，DOD 的融合成本全部计入 `Split scan/mark`；Classic 的 `Mesh emit` 是 dirty-slot 增量更新，不再等同于完整网格构建；GPU shader 仍保留独立 dispatch。

| Algorithm | CPU update | Prepare | Merge mark | Merge topology | Budget leaf collect | Error eval | Split scan/mark | Split topology | Final leaf collect/view | Mesh emit | Finalize | CPU upload |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| Classic CPU ROAM | 313.3471 | 10.0669 | 14.0653 | 40.9936 | 0.0000 | 0.0000 | 42.5828 | 81.5351 | 0.0000 | 60.0972 | 63.0222 | 11.9478 |
| Data-Oriented CPU ROAM | 199.0338 | 9.2513 | 40.2421 | 1.3323 | 0.0000 | 0.0000 | 7.2315 | 88.4044 | 0.0000 | 14.2629 | 38.3083 | 4.0698 |
| GPU ROAM-like | 194.2557 | 9.3653 | 41.1680 | 1.4296 | 0.0000 | 0.0000 | 7.1818 | 90.2034 | 0.0000 | 0.0000 | 44.9063 | 8.2694 |

### 增量 CPU Mesh 输出

`Full rebuilds` 是采样窗口内的全量初始化次数；其余列是逐帧平均值。当前只有 Classic 填充这些字段，其他算法为零不代表它们也采用了增量输出。D3D12 的 frame slot 会延迟消费两次使用之间积累的 dirty range 并集，因此 `Max upload bytes` 表示实际补齐量，不要求等于当前 Build 的 updated triangles。

| Algorithm | Full rebuilds | Updated triangles | Reused triangles | Dirty ranges | Max upload bytes |
| --- | ---: | ---: | ---: | ---: | ---: |
| Classic CPU ROAM | 1 | 116304.4828 | 83695.5172 | 26366.6207 | 33600000 |
| Data-Oriented CPU ROAM | 0 | 0.0000 | 0.0000 | 0.0000 | 33600000 |
| GPU ROAM-like | 0 | 0.0000 | 0.0000 | 0.0000 | 56824820 |

### 原生 pass 包络

这些是各实现原有的外围 pass 计时。它们与上方互斥阶段重叠，不能重复相加。

| Algorithm | Split | Merge | Emit | Validate |
| --- | ---: | ---: | ---: | ---: |
| Classic CPU ROAM | 124.1179 | 55.0589 | 60.0972 | 0.0000 |
| Data-Oriented CPU ROAM | 95.6360 | 41.5744 | 14.2629 | 0.0000 |
| GPU ROAM-like | 97.3852 | 42.5976 | 0.0000 | 0.0000 |

## GPU shader dispatch 明细

每个数值都是一个物理 dispatch 的延迟 GPU timestamp 结果。`Pass sum` 是八个互不重叠区间之和。本表用于解释上方 GPU-like shader 列，不能据此认定所有 ROAM 阶段都已在 GPU 实现。`Dispatch wall`、query wait、readback wait 和 frame-fence wait 都是 CPU 侧开销，不计入 `Pass sum`。

| Algorithm | Pre-split leaf collect | Leaf error / frustum | Split candidate mark | Merge candidate score | Split / direct-diamond commit | Leaf-counter reset | Post-refine leaf collect | Mesh emit / draw args | Pass sum | Max pass sum |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| GPU ROAM-like | 0.0403 | 0.0724 | 0.0210 | 0.0490 | 0.0291 | 0.0092 | 0.0201 | 0.8621 | 1.1033 | 1.9130 |

## GPU 编排与渲染

| Algorithm | Snapshot build | Buffer allocation | Dispatch wall | Query wait | Readback wait | Frame fence wait | Render | Max render | Max upload B | Max readback B |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| Classic CPU ROAM | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 33600000 | 0 |
| Data-Oriented CPU ROAM | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 33600000 | 0 |
| GPU ROAM-like | 12.18 | 2.05 | 0.17 | 0.00 | 12.20 | 0.00 | 0.00 | 0.00 | 56824820 | 96 |
