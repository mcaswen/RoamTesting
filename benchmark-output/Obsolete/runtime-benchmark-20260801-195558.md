# 运行时基准测试报告

- 相机路径：从地形边缘中点移动到地形中心
- 每种算法的采样时长：10 秒
- 详细 CSV：`runtime-benchmark-20260801-195558.csv`

- 构建配置：RelWithDebInfo
- 图形后端：OpenGL
- 图形适配器：NVIDIA GeForce RTX 5090 D/PCIe/SSE2 (4.3.0 NVIDIA 591.86)
- GPU 设备：NVIDIA GeForce RTX 5090 D/PCIe/SSE2 (4.3.0 NVIDIA 591.86)
- GPU 计时模型：先运行 CPU DOD 拓扑基线，再按顺序执行 shader 阶段：split 前 leaf 收集、leaf error 评估、split 候选标记、诊断性 merge 候选评分、split/direct-diamond 拓扑提交、细分后 leaf 收集和 mesh emit；GPU merge 拓扑尚未实现；indirect draw 单独统计
- VSync：基准测试期间已关闭

- Height map：`assets/heightmaps/Hm_Terrain_Test_129.pgm` 129x129
- Terrain size：30
- Height scale：4
- Max depth 设置：14
- ROAM 屏幕空间 split/merge 阈值：4 px / 2 px
- ROAM triangle budget：20000

## 总体结果

| Algorithm | Samples | Avg Frame ms | Max Frame ms | Avg LOD ms | Max LOD ms | Avg Triangles | Max Triangles | Avg Nodes | Max Nodes | Avg CPU % | Max CPU % | Max Workers | Config Max Depth | Reached Max Depth | Max Topology Issues |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| Classic CPU ROAM | 1400 | 7.14 | 26.57 | 6.78 | 22.32 | 12308.25 | 15244 | 42917.34 | 51340 | 91.56 | 689.54 | 1 | 14 | 14 | 0 |
| Data-Oriented CPU ROAM | 2327 | 4.31 | 113.71 | 3.91 | 20.23 | 12284.50 | 15244 | 42971.53 | 51340 | 306.64 | 5352.86 | 8 | 14 | 14 | 0 |
| GPU ROAM-like | 1848 | 5.43 | 25.94 | 5.20 | 24.03 | 12398.63 | 15242 | 42548.98 | 51340 | 168.50 | 3604.49 | 8 | 14 | 14 | 0 |

## ROAM 逻辑阶段对比

表中数值均为平均毫秒数。GPU ROAM-like 是混合路径：先运行完整的 CPU DOD 拓扑基线，再追加一轮 GPU split-only 细分和 GPU mesh emit。两个 GPU-like 列分别展示 CPU 与 shader 的重复职责，避免把 shader 链误解为完整的 GPU ROAM 实现。

| ROAM 逻辑阶段 | Classic CPU | DOD CPU | GPU-like CPU baseline | GPU-like shader | 阶段映射与限制 |
| --- | ---: | ---: | ---: | ---: | --- |
| Prepare / 帧状态 | 0.1453 | 0.1584 | 0.1111 | N/A | CPU 准备持久拓扑和 snapshot 输入 |
| Merge 候选评分 | 0.9246 | 0.9384 | 1.0434 | 0.0046 | Shader 为诊断重新扫描已 split 的父节点；GPU 不提交这些候选 |
| Merge 拓扑提交 / 向上级联 | 0.0037 | 0.0135 | 0.0159 | N/A | 持久 merge、邻接修复和级联回收只由 CPU DOD 执行 |
| Split 前 active leaf 收集 | 0.0891 | 0.0000 | 0.0000 | 0.0045 | DOD CPU 已将 active leaf 判定融合进 Split 候选扫描，本列为 0；GPU 在评分前仍单独压缩 snapshot |
| 视点相关 leaf error / 视锥测试 | 0.0000 | 0.0000 | 0.0000 | 0.0064 | Classic 与 DOD 都在 Split 扫描中内联评估，DOD 本列为 0；GPU 基于 snapshot 单独评分 |
| Split 候选标记 | 0.4193 | 0.2968 | 0.3718 | 0.0036 | DOD CPU 数值包含 active leaf 判定、SSE/视锥评估和 threshold 标记；GPU append 顺序不是误差优先级顺序 |
| Split 拓扑 / 裂缝约束提交 | 0.0075 | 0.0113 | 0.0143 | 0.0030 | GPU 只提交一轮受预算约束的细分，并只处理直接 base-neighbor diamond pair；没有递归 forced-split 链 |
| 细分后 active leaf 收集 | 0.0939 | 0.2383 | 0.2451 | 0.0067 | GPU 数值包含 counter reset 和 split 后 leaf compaction |
| Mesh emit / draw argument 生成 | 4.1383 | 1.0409 | 0.0000 | 0.0104 | GPU 输出非共享顶点、索引和 indirect draw argument |
| Finalize / 发布 packet | 0.6919 | 0.9465 | 0.9418 | N/A | CPU 发布统计和 renderer 资源契约 |

## CPU 实现阶段

`CPU update` 包含下表中互斥的物理执行区间；`CPU upload` 是算法返回后的 renderer 上传。Classic 在扫描和弹出 split queue 时评估屏幕误差；DOD 已把 active leaf 判定、SSE/视锥评估和threshold 标记融合到同一次 node-pool 扫描。因此两者单独的 `Error eval` 都为零，DOD 的融合成本全部计入 `Split mark`；GPU shader 仍保留独立 dispatch。

| Algorithm | CPU update | Prepare | Merge mark | Merge topology | Budget leaf collect | Error eval | Split mark | Split topology | Final leaf collect | Mesh emit | Finalize | CPU upload |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| Classic CPU ROAM | 6.5148 | 0.1453 | 0.9246 | 0.0037 | 0.0891 | 0.0000 | 0.4193 | 0.0075 | 0.0939 | 4.1383 | 0.6919 | 0.2115 |
| Data-Oriented CPU ROAM | 3.6442 | 0.1584 | 0.9384 | 0.0135 | 0.0000 | 0.0000 | 0.2968 | 0.0113 | 0.2383 | 1.0409 | 0.9465 | 0.2105 |
| GPU ROAM-like | 2.7436 | 0.1111 | 1.0434 | 0.0159 | 0.0000 | 0.0000 | 0.3718 | 0.0143 | 0.2451 | 0.0000 | 0.9418 | 0.4658 |

### 原生 pass 包络

这些是各实现原有的外围 pass 计时。它们与上方互斥阶段重叠，不能重复相加。

| Algorithm | Split | Merge | Emit | Validate |
| --- | ---: | ---: | ---: | ---: |
| Classic CPU ROAM | 0.4270 | 0.9292 | 4.2321 | 0.0000 |
| Data-Oriented CPU ROAM | 0.3081 | 0.9519 | 1.2792 | 0.0000 |
| GPU ROAM-like | 0.3861 | 1.0593 | 0.0000 | 0.0000 |

## GPU shader dispatch 明细

每个数值都是一个物理 dispatch 的延迟 GPU timestamp 结果。`Pass sum` 是八个互不重叠区间之和。本表用于解释上方 GPU-like shader 列，不能据此认定所有 ROAM 阶段都已在 GPU 实现。`Dispatch wall`、query wait、readback wait 和 frame-fence wait 都是 CPU 侧开销，不计入 `Pass sum`。

| Algorithm | Pre-split leaf collect | Leaf error / frustum | Split candidate mark | Merge candidate score | Split / direct-diamond commit | Leaf-counter reset | Post-refine leaf collect | Mesh emit / draw args | Pass sum | Max pass sum |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| GPU ROAM-like | 0.0045 | 0.0064 | 0.0036 | 0.0046 | 0.0030 | 0.0028 | 0.0039 | 0.0104 | 0.0393 | 0.4540 |

## GPU 编排与渲染

| Algorithm | Snapshot build | Buffer allocation | Dispatch wall | Query wait | Readback wait | Frame fence wait | Render | Max render | Max upload B | Max readback B |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| Classic CPU ROAM | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0 | 0 |
| Data-Oriented CPU ROAM | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0 | 0 |
| GPU ROAM-like | 0.99 | 0.05 | 0.06 | 0.00 | 0.67 | 0.00 | 0.00 | 0.00 | 5750132 | 96 |
