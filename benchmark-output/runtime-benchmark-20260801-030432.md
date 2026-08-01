# 运行时基准测试报告

- 相机路径：从地形边缘中点移动到地形中心
- 每种算法的采样时长：10 秒
- 详细 CSV：`runtime-benchmark-20260801-030432.csv`

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
| Classic CPU ROAM | 1183 | 8.46 | 90.76 | 7.84 | 24.26 | 12147.59 | 15242 | 43528.55 | 51340 | 93.54 | 634.41 | 1 | 14 | 14 | 0 |
| Data-Oriented CPU ROAM | 1622 | 6.18 | 100.64 | 5.63 | 20.48 | 12436.27 | 15244 | 42412.63 | 51340 | 269.98 | 3461.93 | 8 | 14 | 14 | 0 |
| GPU ROAM-like | 1194 | 8.40 | 31.68 | 8.09 | 23.10 | 12566.69 | 15242 | 41923.48 | 51340 | 196.42 | 2413.31 | 8 | 14 | 14 | 0 |

## ROAM 逻辑阶段对比

表中数值均为平均毫秒数。GPU ROAM-like 是混合路径：先运行完整的 CPU DOD 拓扑基线，再追加一轮 GPU split-only 细分和 GPU mesh emit。两个 GPU-like 列分别展示 CPU 与 shader 的重复职责，避免把 shader 链误解为完整的 GPU ROAM 实现。

| ROAM 逻辑阶段 | Classic CPU | DOD CPU | GPU-like CPU baseline | GPU-like shader | 阶段映射与限制 |
| --- | ---: | ---: | ---: | ---: | --- |
| Prepare / 帧状态 | 0.1516 | 0.2030 | 0.2634 | N/A | CPU 准备持久拓扑和 snapshot 输入 |
| Merge 候选评分 | 1.1476 | 0.9771 | 1.1275 | 0.0046 | Shader 为诊断重新扫描已 split 的父节点；GPU 不提交这些候选 |
| Merge 拓扑提交 / 向上级联 | 0.0049 | 0.0208 | 0.0286 | N/A | 持久 merge、邻接修复和级联回收只由 CPU DOD 执行 |
| Split 前 active leaf 收集 | 0.0920 | 0.2526 | 0.2475 | 0.0043 | GPU 在评分前再次压缩已上传的 CPU 拓扑 |
| 视点相关 leaf error / 视锥测试 | 0.0000 | 0.1960 | 0.2206 | 0.0072 | Classic 在 split 扫描中内联评估；GPU 基于 snapshot 重复执行 DOD 评分 |
| Split 候选标记 | 0.4916 | 1.1656 | 1.2483 | 0.0036 | 按 threshold 和 max depth 分类；GPU append 顺序不是误差优先级顺序 |
| Split 拓扑 / 裂缝约束提交 | 0.0108 | 0.0264 | 0.0325 | 0.0030 | GPU 只提交一轮受预算约束的细分，并只处理直接 base-neighbor diamond pair；没有递归 forced-split 链 |
| 细分后 active leaf 收集 | 0.0882 | 0.2392 | 0.2479 | 0.0066 | GPU 数值包含 counter reset 和 split 后 leaf compaction |
| Mesh emit / draw argument 生成 | 4.4426 | 1.0642 | 0.0000 | 0.0104 | GPU 输出非共享顶点、索引和 indirect draw argument |
| Finalize / 发布 packet | 1.0953 | 1.1783 | 1.4150 | N/A | CPU 发布统计和 renderer 资源契约 |

## CPU 实现阶段

`CPU update` 包含下表中互斥的算法阶段；`CPU upload` 是算法返回后的 renderer 上传。Classic 在扫描和弹出 split queue 时评估屏幕误差，因此单独的 `Error eval` 为零，这部分工作仍计入 `Split mark` / `Split topology`。

| Algorithm | CPU update | Prepare | Merge mark | Merge topology | Budget leaf collect | Error eval | Split mark | Split topology | Final leaf collect | Mesh emit | Finalize | CPU upload |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| Classic CPU ROAM | 7.5261 | 0.1516 | 1.1476 | 0.0049 | 0.0920 | 0.0000 | 0.4916 | 0.0108 | 0.0882 | 4.4426 | 1.0953 | 0.2449 |
| Data-Oriented CPU ROAM | 5.3235 | 0.2030 | 0.9771 | 0.0208 | 0.2526 | 0.1960 | 1.1656 | 0.0264 | 0.2392 | 1.0642 | 1.1783 | 0.2381 |
| GPU ROAM-like | 4.8316 | 0.2634 | 1.1275 | 0.0286 | 0.2475 | 0.2206 | 1.2483 | 0.0325 | 0.2479 | 0.0000 | 1.4150 | 0.4660 |

### 原生 pass 包络

这些是各实现原有的外围 pass 计时。它们与上方互斥阶段重叠，不能重复相加。

| Algorithm | Split | Merge | Emit | Validate |
| --- | ---: | ---: | ---: | ---: |
| Classic CPU ROAM | 0.5027 | 1.1534 | 4.5309 | 0.0000 |
| Data-Oriented CPU ROAM | 1.3880 | 0.9979 | 1.3035 | 0.0000 |
| GPU ROAM-like | 1.5014 | 1.1561 | 0.0000 | 0.0000 |

## GPU shader dispatch 明细

每个数值都是一个物理 dispatch 的延迟 GPU timestamp 结果。`Pass sum` 是八个互不重叠区间之和。本表用于解释上方 GPU-like shader 列，不能据此认定所有 ROAM 阶段都已在 GPU 实现。`Dispatch wall`、query wait、readback wait 和 frame-fence wait 都是 CPU 侧开销，不计入 `Pass sum`。

| Algorithm | Pre-split leaf collect | Leaf error / frustum | Split candidate mark | Merge candidate score | Split / direct-diamond commit | Leaf-counter reset | Post-refine leaf collect | Mesh emit / draw args | Pass sum | Max pass sum |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| GPU ROAM-like | 0.0043 | 0.0072 | 0.0036 | 0.0046 | 0.0030 | 0.0028 | 0.0039 | 0.0104 | 0.0399 | 0.3365 |

## GPU 编排与渲染

| Algorithm | Snapshot build | Buffer allocation | Dispatch wall | Query wait | Readback wait | Frame fence wait | Render | Max render | Max upload B | Max readback B |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| Classic CPU ROAM | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0 | 0 |
| Data-Oriented CPU ROAM | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0 | 0 |
| GPU ROAM-like | 1.03 | 0.00 | 0.13 | 0.00 | 1.19 | 0.00 | 0.00 | 0.00 | 5750132 | 96 |
