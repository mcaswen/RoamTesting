# 运行时基准测试报告

- 相机路径：从地形边缘中点移动到地形中心
- 每种算法的采样时长：10 秒
- 详细 CSV：`runtime-benchmark-20260801-201006.csv`

- 构建配置：RelWithDebInfo
- 图形后端：OpenGL
- 图形适配器：NVIDIA GeForce RTX 5090 D/PCIe/SSE2 (4.3.0 NVIDIA 591.86)
- Benchmark 标签：dod-fused-split-scan
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
| Classic CPU ROAM | 1362 | 7.35 | 26.53 | 6.94 | 22.33 | 12333.64 | 15242 | 42813.84 | 51340 | 92.43 | 652.92 | 1 | 14 | 14 | 0 |
| Data-Oriented CPU ROAM | 1963 | 5.11 | 114.19 | 4.65 | 20.18 | 12354.55 | 15244 | 42766.21 | 51340 | 278.70 | 4681.30 | 8 | 14 | 14 | 0 |
| GPU ROAM-like | 1571 | 6.39 | 31.86 | 6.13 | 27.61 | 12403.14 | 15242 | 42483.91 | 51340 | 163.70 | 3553.56 | 8 | 14 | 14 | 0 |

## ROAM 逻辑阶段对比

表中数值均为平均毫秒数。GPU ROAM-like 是混合路径：先运行完整的 CPU DOD 拓扑基线，再追加一轮 GPU split-only 细分和 GPU mesh emit。两个 GPU-like 列分别展示 CPU 与 shader 的重复职责，避免把 shader 链误解为完整的 GPU ROAM 实现。

| ROAM 逻辑阶段 | Classic CPU | DOD CPU | GPU-like CPU baseline | GPU-like shader | 阶段映射与限制 |
| --- | ---: | ---: | ---: | ---: | --- |
| Prepare / 帧状态 | 0.1649 | 0.2120 | 0.2064 | N/A | CPU 准备持久拓扑和 snapshot 输入 |
| Merge 候选评分 | 0.9821 | 1.0709 | 1.0925 | 0.0047 | Shader 为诊断重新扫描已 split 的父节点；GPU 不提交这些候选 |
| Merge 拓扑提交 / 向上级联 | 0.0040 | 0.0185 | 0.0224 | N/A | 持久 merge、邻接修复和级联回收只由 CPU DOD 执行 |
| Split 前 active leaf 收集 | 0.0905 | 0.0000 | 0.0000 | 0.0044 | DOD CPU 已将 active leaf 判定融合进 Split 候选扫描，本列为 0；GPU 在评分前仍单独压缩 snapshot |
| 视点相关 leaf error / 视锥测试 | 0.0000 | 0.0000 | 0.0000 | 0.0067 | Classic 与 DOD 都在 Split 扫描中内联评估，DOD 本列为 0；GPU 基于 snapshot 单独评分 |
| Split 扫描/标记 | 0.4256 | 0.4196 | 0.4478 | 0.0037 | DOD CPU 数值包含 active leaf 判定、SSE/视锥评估和 threshold 标记；GPU append 顺序不是误差优先级顺序 |
| Split 拓扑 / 裂缝约束提交 | 0.0080 | 0.0168 | 0.0227 | 0.0030 | GPU 只提交一轮受预算约束的细分，并只处理直接 base-neighbor diamond pair；没有递归 forced-split 链 |
| 细分后 active leaf 收集 | 0.0929 | 0.3036 | 0.2979 | 0.0067 | GPU 数值包含 counter reset 和 split 后 leaf compaction |
| Mesh emit / draw argument 生成 | 4.1872 | 1.2708 | 0.0000 | 0.0104 | GPU 输出非共享顶点、索引和 indirect draw argument |
| Finalize / 发布 packet | 0.7184 | 1.0632 | 1.2530 | N/A | CPU 发布统计和 renderer 资源契约 |

## CPU 实现阶段

`CPU update` 包含下表中互斥的物理执行区间；`CPU upload` 是算法返回后的 renderer 上传。Classic 在扫描和弹出 split queue 时评估屏幕误差；DOD 已把 active leaf 遍历、SSE/视锥评估和 threshold 标记融合到同一次 active-leaf-index 扫描。因此两者单独的 `Error eval` 都为零，DOD 的融合成本全部计入 `Split scan/mark`；GPU shader 仍保留独立 dispatch。

| Algorithm | CPU update | Prepare | Merge mark | Merge topology | Budget leaf collect | Error eval | Split scan/mark | Split topology | Final leaf collect | Mesh emit | Finalize | CPU upload |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| Classic CPU ROAM | 6.6748 | 0.1649 | 0.9821 | 0.0040 | 0.0905 | 0.0000 | 0.4256 | 0.0080 | 0.0929 | 4.1872 | 0.7184 | 0.2120 |
| Data-Oriented CPU ROAM | 4.3755 | 0.2120 | 1.0709 | 0.0185 | 0.0000 | 0.0000 | 0.4196 | 0.0168 | 0.3036 | 1.2708 | 1.0632 | 0.2152 |
| GPU ROAM-like | 3.3429 | 0.2064 | 1.0925 | 0.0224 | 0.0000 | 0.0000 | 0.4478 | 0.0227 | 0.2979 | 0.0000 | 1.2530 | 0.4461 |

### 原生 pass 包络

这些是各实现原有的外围 pass 计时。它们与上方互斥阶段重叠，不能重复相加。

| Algorithm | Split | Merge | Emit | Validate |
| --- | ---: | ---: | ---: | ---: |
| Classic CPU ROAM | 0.4339 | 0.9870 | 4.2801 | 0.0000 |
| Data-Oriented CPU ROAM | 0.4363 | 1.0894 | 1.5744 | 0.0000 |
| GPU ROAM-like | 0.4705 | 1.1148 | 0.0000 | 0.0000 |

## GPU shader dispatch 明细

每个数值都是一个物理 dispatch 的延迟 GPU timestamp 结果。`Pass sum` 是八个互不重叠区间之和。本表用于解释上方 GPU-like shader 列，不能据此认定所有 ROAM 阶段都已在 GPU 实现。`Dispatch wall`、query wait、readback wait 和 frame-fence wait 都是 CPU 侧开销，不计入 `Pass sum`。

| Algorithm | Pre-split leaf collect | Leaf error / frustum | Split candidate mark | Merge candidate score | Split / direct-diamond commit | Leaf-counter reset | Post-refine leaf collect | Mesh emit / draw args | Pass sum | Max pass sum |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| GPU ROAM-like | 0.0044 | 0.0067 | 0.0037 | 0.0047 | 0.0030 | 0.0028 | 0.0039 | 0.0104 | 0.0396 | 0.0937 |

## GPU 编排与渲染

| Algorithm | Snapshot build | Buffer allocation | Dispatch wall | Query wait | Readback wait | Frame fence wait | Render | Max render | Max upload B | Max readback B |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| Classic CPU ROAM | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0 | 0 |
| Data-Oriented CPU ROAM | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0 | 0 |
| GPU ROAM-like | 1.01 | 0.03 | 0.08 | 0.00 | 0.94 | 0.00 | 0.00 | 0.00 | 5750132 | 96 |
