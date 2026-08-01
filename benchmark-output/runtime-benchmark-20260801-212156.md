# 运行时基准测试报告

- 相机路径：从地形边缘中点移动到地形中心
- 每种算法的采样时长：10 秒
- 详细 CSV：`runtime-benchmark-20260801-212156.csv`

- 构建配置：RelWithDebInfo
- 图形后端：OpenGL
- 图形适配器：NVIDIA GeForce RTX 5090 D/PCIe/SSE2 (4.3.0 NVIDIA 591.86)
- GPU 设备：NVIDIA GeForce RTX 5090 D/PCIe/SSE2 (4.3.0 NVIDIA 591.86)
- GPU 计时模型：先运行 CPU DOD 拓扑基线，再按顺序执行 shader 阶段：split 前 leaf 收集、leaf error 评估、split 候选标记、诊断性 merge 候选评分、split/direct-diamond 拓扑提交、细分后 leaf 收集和 mesh emit；GPU merge 拓扑尚未实现；indirect draw 单独统计
- VSync：基准测试期间已关闭

- Height map：`assets/heightmaps/Hm_Terrain_Test_129.pgm` 129x129
- Terrain size：39.4
- Height scale：12
- Max depth 设置：20
- ROAM 屏幕空间 split/merge 阈值：4 px / 2 px
- ROAM triangle budget：200000

## 总体结果

| Algorithm | Samples | Avg Frame ms | Max Frame ms | Avg LOD ms | Max LOD ms | Avg Triangles | Max Triangles | Avg Nodes | Max Nodes | Avg CPU % | Max CPU % | Max Workers | Config Max Depth | Reached Max Depth | Max Topology Issues |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| Classic CPU ROAM | 241 | 41.62 | 515.39 | 41.10 | 481.19 | 45181.16 | 50452 | 187904.57 | 261144 | 99.92 | 150.90 | 1 | 20 | 19 | 0 |
| Data-Oriented CPU ROAM | 566 | 18.59 | 506.63 | 17.28 | 472.24 | 45339.64 | 50490 | 186770.69 | 261880 | 241.15 | 994.38 | 8 | 20 | 19 | 0 |
| GPU ROAM-like | 368 | 28.56 | 490.37 | 26.75 | 474.56 | 46222.95 | 50492 | 173774.17 | 261568 | 150.61 | 703.43 | 8 | 20 | 19 | 0 |

## ROAM 逻辑阶段对比

表中数值均为平均毫秒数。GPU ROAM-like 是混合路径：先运行完整的 CPU DOD 拓扑基线，再追加一轮 GPU split-only 细分和 GPU mesh emit。两个 GPU-like 列分别展示 CPU 与 shader 的重复职责，避免把 shader 链误解为完整的 GPU ROAM 实现。

| ROAM 逻辑阶段 | Classic CPU | DOD CPU | GPU-like CPU baseline | GPU-like shader | 阶段映射与限制 |
| --- | ---: | ---: | ---: | ---: | --- |
| Prepare / 帧状态 | 2.1616 | 1.1351 | 2.4244 | N/A | CPU 准备持久拓扑和 snapshot 输入 |
| Merge 候选评分 | 8.6257 | 4.9363 | 5.5778 | 0.0100 | Shader 为诊断重新扫描已 split 的父节点；GPU 不提交这些候选 |
| Merge 拓扑提交 / 向上级联 | 0.1217 | 0.1965 | 0.2736 | N/A | 持久 merge、邻接修复和级联回收只由 CPU DOD 执行 |
| Split 前 active leaf 收集 | 0.5860 | 0.0000 | 0.0000 | 0.0133 | DOD CPU 已将 active leaf 遍历与计数融合进 Split 候选扫描，本列为 0；GPU 在评分前仍单独压缩 snapshot |
| 视点相关 leaf error / 视锥测试 | 0.0000 | 0.0000 | 0.0000 | 0.0097 | Classic 与 DOD 都在 Split 扫描中内联评估，DOD 本列为 0；GPU 基于 snapshot 单独评分 |
| Split 扫描/标记 | 5.9612 | 1.4202 | 1.4684 | 0.0066 | DOD CPU 数值包含 active leaf 遍历、SSE/视锥评估和 threshold 标记；GPU append 顺序不是误差优先级顺序 |
| Split 拓扑 / 裂缝约束提交 | 0.2752 | 0.2758 | 0.4111 | 0.0043 | GPU 只提交一轮受预算约束的细分，并只处理直接 base-neighbor diamond pair；没有递归 forced-split 链 |
| 细分后 active leaf 收集 | 0.4994 | 0.8816 | 0.8677 | 0.0116 | GPU 数值包含 counter reset 和 split 后 leaf compaction |
| Mesh emit / draw argument 生成 | 16.0620 | 3.1030 | 0.0000 | 0.0266 | GPU 输出非共享顶点、索引和 indirect draw argument |
| Finalize / 发布 packet | 6.0167 | 4.6281 | 5.0052 | N/A | CPU 发布统计和 renderer 资源契约 |

## CPU 实现阶段

`CPU update` 包含下表中互斥的物理执行区间；`CPU upload` 是算法返回后的 renderer 上传。Classic 在扫描和弹出 split queue 时评估屏幕误差；DOD 已把 active leaf 遍历、SSE/视锥评估和 threshold 标记融合到同一次 active-leaf-index 扫描。因此两者单独的 `Error eval` 都为零，DOD 的融合成本全部计入 `Split scan/mark`；GPU shader 仍保留独立 dispatch。

| Algorithm | CPU update | Prepare | Merge mark | Merge topology | Budget leaf collect | Error eval | Split scan/mark | Split topology | Final leaf collect | Mesh emit | Finalize | CPU upload |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| Classic CPU ROAM | 40.3298 | 2.1616 | 8.6257 | 0.1217 | 0.5860 | 0.0000 | 5.9612 | 0.2752 | 0.4994 | 16.0620 | 6.0167 | 0.5455 |
| Data-Oriented CPU ROAM | 16.5768 | 1.1351 | 4.9363 | 0.1965 | 0.0000 | 0.0000 | 1.4202 | 0.2758 | 0.8816 | 3.1030 | 4.6281 | 0.4676 |
| GPU ROAM-like | 16.0283 | 2.4244 | 5.5778 | 0.2736 | 0.0000 | 0.0000 | 1.4684 | 0.4111 | 0.8677 | 0.0000 | 5.0052 | 2.9728 |

### 原生 pass 包络

这些是各实现原有的外围 pass 计时。它们与上方互斥阶段重叠，不能重复相加。

| Algorithm | Split | Merge | Emit | Validate |
| --- | ---: | ---: | ---: | ---: |
| Classic CPU ROAM | 6.2371 | 8.7668 | 16.5614 | 0.0000 |
| Data-Oriented CPU ROAM | 1.6960 | 5.1328 | 3.9846 | 0.0000 |
| GPU ROAM-like | 1.8795 | 5.8514 | 0.0000 | 0.0000 |

## GPU shader dispatch 明细

每个数值都是一个物理 dispatch 的延迟 GPU timestamp 结果。`Pass sum` 是八个互不重叠区间之和。本表用于解释上方 GPU-like shader 列，不能据此认定所有 ROAM 阶段都已在 GPU 实现。`Dispatch wall`、query wait、readback wait 和 frame-fence wait 都是 CPU 侧开销，不计入 `Pass sum`。

| Algorithm | Pre-split leaf collect | Leaf error / frustum | Split candidate mark | Merge candidate score | Split / direct-diamond commit | Leaf-counter reset | Post-refine leaf collect | Mesh emit / draw args | Pass sum | Max pass sum |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| GPU ROAM-like | 0.0133 | 0.0097 | 0.0066 | 0.0100 | 0.0043 | 0.0037 | 0.0079 | 0.0266 | 0.0822 | 0.1508 |

## GPU 编排与渲染

| Algorithm | Snapshot build | Buffer allocation | Dispatch wall | Query wait | Readback wait | Frame fence wait | Render | Max render | Max upload B | Max readback B |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| Classic CPU ROAM | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0 | 0 |
| Data-Oriented CPU ROAM | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0 | 0 |
| GPU ROAM-like | 3.93 | 1.18 | 0.10 | 0.00 | 1.82 | 0.00 | 0.00 | 0.00 | 29295668 | 96 |
