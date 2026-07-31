# 运行时基准测试报告

- 相机路径：从地形边缘中点移动到地形中心
- 每种算法的采样时长：1 秒
- 详细 CSV：`runtime-benchmark-20260731-221234.csv`

- 构建配置：RelWithDebInfo
- 图形后端：D3D12
- 图形适配器：NVIDIA GeForce RTX 5090 D (Direct3D 12 (feature level 12_0); requested Agility SDK 614; runtime 6.2.26100.8737 (C:\WINDOWS\SYSTEM32\D3D12Core.dll); driver 32.0.15.9186)
- Benchmark 标签：roam-stage-aligned-d3d12
- GPU 计时模型：CPU DOD 拓扑基线加有序 DX12 shader 阶段：split 前 leaf 收集、leaf error 评估、split 候选标记、诊断性 merge 候选评分、split/direct-diamond 拓扑提交、细分后 leaf 收集和 mesh emit；GPU merge 拓扑尚未实现；ExecuteIndirect 单独统计
- CBT 2024 procedural 验证可用，但在拓扑迁移完成前不纳入本报告
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
| Classic CPU ROAM | 141 | 7.11 | 45.27 | 6.75 | 22.85 | 12130.85 | 15235 | 43573.69 | 51340 | 90.98 | 362.75 | 1 | 14 | 14 | 0 |
| Data-Oriented CPU ROAM | 205 | 4.99 | 22.89 | 4.67 | 18.95 | 12510.84 | 15241 | 42213.88 | 51340 | 294.68 | 3001.78 | 8 | 14 | 14 | 0 |
| GPU ROAM-like | 70 | 14.41 | 172.49 | 14.19 | 171.27 | 12336.23 | 15199 | 43645.49 | 51324 | 158.21 | 1096.08 | 8 | 14 | 14 | 0 |

## ROAM 逻辑阶段对比

表中数值均为平均毫秒数。GPU ROAM-like 是混合路径：先运行完整的 CPU DOD 拓扑基线，再追加一轮 GPU split-only 细分和 GPU mesh emit。两个 GPU-like 列分别展示 CPU 与 shader 的重复职责，避免把 shader 链误解为完整的 GPU ROAM 实现。

| ROAM 逻辑阶段 | Classic CPU | DOD CPU | GPU-like CPU baseline | GPU-like shader | GPU-like 实现状态 |
| --- | ---: | ---: | ---: | ---: | --- |
| Prepare / 帧状态 | 0.1539 | 0.1444 | 0.2860 | N/A | CPU 准备持久拓扑和 snapshot 输入 |
| Merge 候选评分 | 0.6672 | 0.7342 | 1.0786 | 0.0029 | Shader 为诊断重新扫描已 split 的父节点；GPU 不提交这些候选 |
| Merge 拓扑提交 / 向上级联 | 0.0290 | 0.0781 | 0.1723 | N/A | 持久 merge、邻接修复和级联回收只由 CPU DOD 执行 |
| Split 前 active leaf 收集 | 0.0968 | 0.2304 | 0.2095 | 0.0036 | GPU 在评分前再次压缩已上传的 CPU 拓扑 |
| 视点相关 leaf error / 视锥测试 | 0.0000 | 0.1551 | 0.1839 | 0.0025 | Classic 在 split 扫描中内联评估；GPU 基于 snapshot 重复执行 DOD 评分 |
| Split 候选标记 | 0.4571 | 1.0268 | 1.0473 | 0.0017 | 按 threshold 和 max depth 分类；GPU append 顺序不是误差优先级顺序 |
| Split 拓扑 / 裂缝约束提交 | 0.0675 | 0.0960 | 0.2756 | 0.0011 | GPU 只提交一轮受预算约束的细分，并只处理直接 base-neighbor diamond pair；没有递归 forced-split 链 |
| 细分后 active leaf 收集 | 0.0971 | 0.2124 | 0.2343 | 0.0028 | GPU 数值包含 counter reset 和 split 后 leaf compaction |
| Mesh emit / draw argument 生成 | 4.1481 | 0.8604 | 0.0000 | 0.0077 | GPU 输出非共享顶点、索引和 indirect draw argument |
| Finalize / 发布 packet | 0.8171 | 0.9460 | 1.2004 | N/A | CPU 发布统计和 renderer 资源契约 |

## CPU 实现阶段

`CPU update` 包含下表中互斥的算法阶段；`CPU upload` 是算法返回后的 renderer 上传。Classic 在扫描和弹出 split queue 时评估屏幕误差，因此单独的 `Error eval` 为零，这部分工作仍计入 `Split mark` / `Split topology`。

| Algorithm | CPU update | Prepare | Merge mark | Merge topology | Budget leaf collect | Error eval | Split mark | Split topology | Final leaf collect | Mesh emit | Validate | Finalize | CPU upload |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| Classic CPU ROAM | 6.5344 | 0.1539 | 0.6672 | 0.0290 | 0.0968 | 0.0000 | 0.4571 | 0.0675 | 0.0971 | 4.1481 | 0.0000 | 0.8171 | 0.1361 |
| Data-Oriented CPU ROAM | 4.4839 | 0.1444 | 0.7342 | 0.0781 | 0.2304 | 0.1551 | 1.0268 | 0.0960 | 0.2124 | 0.8604 | 0.0000 | 0.9460 | 0.1287 |
| GPU ROAM-like | 4.6882 | 0.2860 | 1.0786 | 0.1723 | 0.2095 | 0.1839 | 1.0473 | 0.2756 | 0.2343 | 0.0000 | 0.0000 | 1.2004 | 0.0000 |

### 原生 pass 包络

这些是各实现原有的外围 pass 计时。它们与上方互斥阶段重叠，不能重复相加。

| Algorithm | Split | Merge | Emit | Validate |
| --- | ---: | ---: | ---: | ---: |
| Classic CPU ROAM | 0.5248 | 0.6964 | 4.2452 | 0.0000 |
| Data-Oriented CPU ROAM | 1.2779 | 0.8123 | 1.0727 | 0.0000 |
| GPU ROAM-like | 1.5068 | 1.2509 | 0.0000 | 0.0000 |

## GPU shader dispatch 明细

每个数值都是一个物理 dispatch 的延迟 GPU timestamp 结果。`Pass sum` 是八个互不重叠区间之和。本表用于解释上方 GPU-like shader 列，不能据此认定所有 ROAM 阶段都已在 GPU 实现。`Dispatch wall`、query wait、readback wait 和 frame-fence wait 都是 CPU 侧开销，不计入 `Pass sum`。

| Algorithm | Pre-split leaf collect | Leaf error / frustum | Split candidate mark | Merge candidate score | Split / direct-diamond commit | Leaf-counter reset | Post-refine leaf collect | Mesh emit / draw args | Pass sum | Max pass sum |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| Classic CPU ROAM | 0.0000 | 0.0000 | 0.0000 | 0.0000 | 0.0000 | 0.0000 | 0.0000 | 0.0000 | 0.0000 | 0.0000 |
| Data-Oriented CPU ROAM | 0.0000 | 0.0000 | 0.0000 | 0.0000 | 0.0000 | 0.0000 | 0.0000 | 0.0000 | 0.0000 | 0.0000 |
| GPU ROAM-like | 0.0036 | 0.0025 | 0.0017 | 0.0029 | 0.0011 | 0.0008 | 0.0020 | 0.0077 | 0.0224 | 0.0248 |

## GPU 编排与渲染

| Algorithm | Snapshot build | Buffer allocation | Dispatch wall | Query wait | Readback wait | Frame fence wait | Render | Max render | Max upload B | Max readback B |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| Classic CPU ROAM | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.01 | 0.01 | 0 | 0 |
| Data-Oriented CPU ROAM | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.01 | 0.01 | 0 | 0 |
| GPU ROAM-like | 1.12 | 5.86 | 0.27 | 0.00 | 0.00 | 0.00 | 0.05 | 0.06 | 5748320 | 104 |
