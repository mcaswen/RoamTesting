# 运行时基准测试报告

- 相机路径：从地形边缘中点移动到地形中心
- 每种算法的采样时长：10 秒
- 详细 CSV：`runtime-benchmark-20260804-030248.csv`

- 构建配置：RelWithDebInfo
- 图形后端：OpenGL
- 图形适配器：NVIDIA GeForce RTX 5090 D/PCIe/SSE2 (4.3.0 NVIDIA 591.86)
- GPU 设备：NVIDIA GeForce RTX 5090 D/PCIe/SSE2 (4.3.0 NVIDIA 591.86)
- GPU 计时模型：先运行 CPU DOD 拓扑基线，再按顺序执行 shader 阶段：split 前 leaf 收集、leaf error 评估、split 候选标记、诊断性 merge 候选评分、split/direct-diamond 拓扑提交、细分后 leaf 收集和 mesh emit；GPU merge 拓扑尚未实现；indirect draw 单独统计
- VSync：基准测试期间已关闭

- Height map：`assets/heightmaps/Hm_Terrain_Test_129.pgm` 129x129
- Terrain size：24.7
- Height scale：12
- Max depth 设置：20
- ROAM 屏幕空间 split/merge 阈值：4 px / 2 px
- ROAM triangle budget：200000

## 总体结果

| Algorithm | Samples | Avg Frame ms | Max Frame ms | Avg LOD ms | Max LOD ms | Avg Triangles | Max Triangles | Avg Nodes | Max Nodes | Avg CPU % | Max CPU % | Max Workers | Config Max Depth | Reached Max Depth | Max Topology Issues |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| Classic CPU ROAM | 1514 | 6.61 | 195.56 | 6.36 | 167.50 | 28215.90 | 35081 | 102555.54 | 127742 | 98.91 | 370.77 | 1 | 20 | 17 | 0 |
| Data-Oriented CPU ROAM | 950 | 10.66 | 127.96 | 10.26 | 127.40 | 28629.13 | 35083 | 100752.53 | 127694 | 232.44 | 1728.07 | 8 | 20 | 17 | 0 |
| GPU ROAM-like | 691 | 14.67 | 131.86 | 14.05 | 130.83 | 29207.96 | 35083 | 98250.29 | 127650 | 168.89 | 1196.24 | 8 | 20 | 17 | 0 |

## ROAM 逻辑阶段对比

表中数值均为平均毫秒数。GPU ROAM-like 是混合路径：先运行完整的 CPU DOD 拓扑基线，再追加一轮 GPU split-only 细分和 GPU mesh emit。两个 GPU-like 列分别展示 CPU 与 shader 的重复职责，避免把 shader 链误解为完整的 GPU ROAM 实现。

| ROAM 逻辑阶段 | Classic CPU | DOD CPU | GPU-like CPU baseline | GPU-like shader | 阶段映射与限制 |
| --- | ---: | ---: | ---: | ---: | --- |
| Prepare / 帧状态 | 0.2748 | 0.3202 | 0.4982 | N/A | CPU 准备持久拓扑和 snapshot 输入 |
| Merge 候选评分 | 1.0559 | 4.1185 | 4.2705 | 0.0073 | Shader 为诊断重新扫描已 split 的父节点；GPU 不提交这些候选 |
| Merge 拓扑提交 / 向上级联 | 0.0235 | 0.0584 | 0.0730 | N/A | 持久 merge、邻接修复和级联回收只由 CPU DOD 执行 |
| Split 前 active leaf 收集 | 0.0000 | 0.0000 | 0.0000 | 0.0078 | DOD CPU 已将 active leaf 遍历与计数融合进 Split 候选扫描，本列为 0；GPU 在评分前仍单独压缩 snapshot |
| 视点相关 leaf error / 视锥测试 | 0.0000 | 0.0000 | 0.0000 | 0.0074 | Classic 与 DOD 都在 Split 扫描中内联评估，DOD 本列为 0；GPU 基于 snapshot 单独评分 |
| Split 扫描/标记 | 3.0760 | 1.1005 | 1.1494 | 0.0057 | DOD CPU 数值包含 active leaf 遍历、SSE/视锥评估和 threshold 标记；GPU append 顺序不是误差优先级顺序 |
| Split 拓扑 / 裂缝约束提交 | 0.0661 | 0.0808 | 0.1087 | 0.0035 | GPU 只提交一轮受预算约束的细分，并只处理直接 base-neighbor diamond pair；没有递归 forced-split 链 |
| 细分后 active leaf 收集 / 输出视图 | 0.0000 | 0.0000 | 0.0000 | 0.0090 | Classic 直接复用 dense mesh slot owners，DOD 直接复用 ActiveLeafNodes，因此两者本列均为 0；GPU 数值包含 counter reset 和 split 后 leaf compaction |
| Mesh emit / draw argument 生成 | 0.0485 | 1.9972 | 0.0000 | 0.0195 | Classic 只重写拓扑变化影响的稳定槽位；DOD 仍完整 emit；GPU 输出非共享顶点、索引和 indirect draw argument |
| Finalize / 发布 packet | 1.7779 | 2.2525 | 2.6384 | N/A | CPU 发布统计和 renderer 资源契约 |

## CPU 实现阶段

`CPU update` 包含下表中互斥的物理执行区间；`CPU upload` 是算法返回后的 renderer 上传。Classic 在扫描和弹出 split queue 时评估屏幕误差；DOD 已把 active leaf 遍历、SSE/视锥评估和 threshold 标记融合到同一次 active-leaf-index 扫描。因此两者单独的 `Error eval` 都为零，DOD 的融合成本全部计入 `Split scan/mark`；Classic 的 `Mesh emit` 是 dirty-slot 增量更新，不再等同于完整网格构建；GPU shader 仍保留独立 dispatch。

| Algorithm | CPU update | Prepare | Merge mark | Merge topology | Budget leaf collect | Error eval | Split scan/mark | Split topology | Final leaf collect/view | Mesh emit | Finalize | CPU upload |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| Classic CPU ROAM | 6.3239 | 0.2748 | 1.0559 | 0.0235 | 0.0000 | 0.0000 | 3.0760 | 0.0661 | 0.0000 | 0.0485 | 1.7779 | 0.0344 |
| Data-Oriented CPU ROAM | 9.9283 | 0.3202 | 4.1185 | 0.0584 | 0.0000 | 0.0000 | 1.1005 | 0.0808 | 0.0000 | 1.9972 | 2.2525 | 0.2124 |
| GPU ROAM-like | 8.7384 | 0.4982 | 4.2705 | 0.0730 | 0.0000 | 0.0000 | 1.1494 | 0.1087 | 0.0000 | 0.0000 | 2.6384 | 1.2870 |

### 增量 CPU Mesh 输出

`Full rebuilds` 是采样窗口内的全量初始化次数；其余列是逐帧平均值。当前只有 Classic 填充这些字段，其他算法为零不代表它们也采用了增量输出。D3D12 的 frame slot 会延迟消费两次使用之间积累的 dirty range 并集，因此 `Max upload bytes` 表示实际补齐量，不要求等于当前 Build 的 updated triangles。

| Algorithm | Full rebuilds | Updated triangles | Reused triangles | Dirty ranges | Max upload bytes |
| --- | ---: | ---: | ---: | ---: | ---: |
| Classic CPU ROAM | 1 | 165.9458 | 28049.9511 | 84.3362 | 5891592 |
| Data-Oriented CPU ROAM | 0 | 0.0000 | 0.0000 | 0.0000 | 5893944 |
| GPU ROAM-like | 0 | 0.0000 | 0.0000 | 0.0000 | 14296852 |

### 原生 pass 包络

这些是各实现原有的外围 pass 计时。它们与上方互斥阶段重叠，不能重复相加。

| Algorithm | Split | Merge | Emit | Validate |
| --- | ---: | ---: | ---: | ---: |
| Classic CPU ROAM | 3.1421 | 1.0794 | 0.0485 | 0.0000 |
| Data-Oriented CPU ROAM | 1.1814 | 4.1769 | 1.9972 | 0.0000 |
| GPU ROAM-like | 1.2581 | 4.3435 | 0.0000 | 0.0000 |

## GPU shader dispatch 明细

每个数值都是一个物理 dispatch 的延迟 GPU timestamp 结果。`Pass sum` 是八个互不重叠区间之和。本表用于解释上方 GPU-like shader 列，不能据此认定所有 ROAM 阶段都已在 GPU 实现。`Dispatch wall`、query wait、readback wait 和 frame-fence wait 都是 CPU 侧开销，不计入 `Pass sum`。

| Algorithm | Pre-split leaf collect | Leaf error / frustum | Split candidate mark | Merge candidate score | Split / direct-diamond commit | Leaf-counter reset | Post-refine leaf collect | Mesh emit / draw args | Pass sum | Max pass sum |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| GPU ROAM-like | 0.0078 | 0.0074 | 0.0057 | 0.0073 | 0.0035 | 0.0028 | 0.0062 | 0.0195 | 0.0602 | 0.4940 |

## GPU 编排与渲染

| Algorithm | Snapshot build | Buffer allocation | Dispatch wall | Query wait | Readback wait | Frame fence wait | Render | Max render | Max upload B | Max readback B |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| Classic CPU ROAM | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 5891592 | 0 |
| Data-Oriented CPU ROAM | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 5893944 | 0 |
| GPU ROAM-like | 2.34 | 0.20 | 0.06 | 0.00 | 1.16 | 0.00 | 0.00 | 0.00 | 14296852 | 96 |
