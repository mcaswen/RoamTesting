# 运行时基准测试报告

- 相机路径：从地形边缘中点移动到地形中心
- 每种算法的采样时长：10 秒
- 详细 CSV：`runtime-benchmark-20260801-020956.csv`

- 构建配置：RelWithDebInfo
- 图形后端：OpenGL
- 图形适配器：NVIDIA GeForce RTX 5090 D/PCIe/SSE2 (4.3.0 NVIDIA 591.86)
- GPU 设备：NVIDIA GeForce RTX 5090 D/PCIe/SSE2 (4.3.0 NVIDIA 591.86)
- GPU 计时模型：先运行 CPU DOD 拓扑基线，再按顺序执行 shader 阶段：split 前 leaf 收集、leaf error 评估、split 候选标记、诊断性 merge 候选评分、split/direct-diamond 拓扑提交、细分后 leaf 收集和 mesh emit；GPU merge 拓扑尚未实现；indirect draw 单独统计
- VSync：基准测试期间已关闭

- Height map：`assets/heightmaps/Hm_Terrain_Test_129.pgm` 129x129
- Terrain size：80
- Height scale：12
- Max depth 设置：20
- ROAM 屏幕空间 split/merge 阈值：4 px / 2 px
- ROAM triangle budget：200000

## 总体结果

| Algorithm | Samples | Avg Frame ms | Max Frame ms | Avg LOD ms | Max LOD ms | Avg Triangles | Max Triangles | Avg Nodes | Max Nodes | Avg CPU % | Max CPU % | Max Workers | Config Max Depth | Reached Max Depth | Max Topology Issues |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| Classic CPU ROAM | 590 | 16.97 | 452.65 | 16.47 | 448.41 | 20997.87 | 22298 | 109113.26 | 170306 | 97.66 | 202.60 | 1 | 20 | 18 | 0 |
| Data-Oriented CPU ROAM | 858 | 12.22 | 484.94 | 11.27 | 440.93 | 21036.17 | 22282 | 106963.10 | 170522 | 266.08 | 1357.90 | 8 | 20 | 18 | 0 |
| GPU ROAM-like | 564 | 18.62 | 482.51 | 17.29 | 448.33 | 21235.25 | 22286 | 98169.02 | 170322 | 167.68 | 1108.12 | 8 | 20 | 18 | 0 |

## ROAM 逻辑阶段对比

表中数值均为平均毫秒数。GPU ROAM-like 是混合路径：先运行完整的 CPU DOD 拓扑基线，再追加一轮 GPU split-only 细分和 GPU mesh emit。两个 GPU-like 列分别展示 CPU 与 shader 的重复职责，避免把 shader 链误解为完整的 GPU ROAM 实现。

| ROAM 逻辑阶段 | Classic CPU | DOD CPU | GPU-like CPU baseline | GPU-like shader | 阶段映射与限制 |
| --- | ---: | ---: | ---: | ---: | --- |
| Prepare / 帧状态 | 0.9113 | 0.6753 | 1.2287 | N/A | CPU 准备持久拓扑和 snapshot 输入 |
| Merge 候选评分 | 3.1211 | 3.2906 | 3.5305 | 0.0070 | Shader 为诊断重新扫描已 split 的父节点；GPU 不提交这些候选 |
| Merge 拓扑提交 / 向上级联 | 0.0328 | 0.0892 | 0.1278 | N/A | 持久 merge、邻接修复和级联回收只由 CPU DOD 执行 |
| Split 前 active leaf 收集 | 0.1746 | 0.3872 | 0.3697 | 0.0092 | GPU 在评分前再次压缩已上传的 CPU 拓扑 |
| 视点相关 leaf error / 视锥测试 | 0.0000 | 0.3010 | 0.3224 | 0.0077 | Classic 在 split 扫描中内联评估；GPU 基于 snapshot 重复执行 DOD 评分 |
| Split 候选标记 | 2.0990 | 2.4728 | 2.4255 | 0.0057 | 按 threshold 和 max depth 分类；GPU append 顺序不是误差优先级顺序 |
| Split 拓扑 / 裂缝约束提交 | 0.0685 | 0.1238 | 0.1840 | 0.0034 | GPU 只提交一轮受预算约束的细分，并只处理直接 base-neighbor diamond pair；没有递归 forced-split 链 |
| 细分后 active leaf 收集 | 0.1843 | 0.3811 | 0.3726 | 0.0091 | GPU 数值包含 counter reset 和 split 后 leaf compaction |
| Mesh emit / draw argument 生成 | 7.1294 | 1.3374 | 0.0000 | 0.0156 | GPU 输出非共享顶点、索引和 indirect draw argument |
| Finalize / 发布 packet | 2.4129 | 1.9174 | 2.5036 | N/A | CPU 发布统计和 renderer 资源契约 |

## CPU 实现阶段

`CPU update` 包含下表中互斥的算法阶段；`CPU upload` 是算法返回后的 renderer 上传。Classic 在扫描和弹出 split queue 时评估屏幕误差，因此单独的 `Error eval` 为零，这部分工作仍计入 `Split mark` / `Split topology`。

| Algorithm | CPU update | Prepare | Merge mark | Merge topology | Budget leaf collect | Error eval | Split mark | Split topology | Final leaf collect | Mesh emit | Finalize | CPU upload |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| Classic CPU ROAM | 16.1417 | 0.9113 | 3.1211 | 0.0328 | 0.1746 | 0.0000 | 2.0990 | 0.0685 | 0.1843 | 7.1294 | 2.4129 | 0.2191 |
| Data-Oriented CPU ROAM | 10.9760 | 0.6753 | 3.2906 | 0.0892 | 0.3872 | 0.3010 | 2.4728 | 0.1238 | 0.3811 | 1.3374 | 1.9174 | 0.1795 |
| GPU ROAM-like | 11.0651 | 1.2287 | 3.5305 | 0.1278 | 0.3697 | 0.3224 | 2.4255 | 0.1840 | 0.3726 | 0.0000 | 2.5036 | 1.7150 |

### 原生 pass 包络

这些是各实现原有的外围 pass 计时。它们与上方互斥阶段重叠，不能重复相加。

| Algorithm | Split | Merge | Emit | Validate |
| --- | ---: | ---: | ---: | ---: |
| Classic CPU ROAM | 2.1678 | 3.1613 | 7.3136 | 0.0000 |
| Data-Oriented CPU ROAM | 2.8975 | 3.3798 | 1.7185 | 0.0000 |
| GPU ROAM-like | 2.9319 | 3.6583 | 0.0000 | 0.0000 |

## GPU shader dispatch 明细

每个数值都是一个物理 dispatch 的延迟 GPU timestamp 结果。`Pass sum` 是八个互不重叠区间之和。本表用于解释上方 GPU-like shader 列，不能据此认定所有 ROAM 阶段都已在 GPU 实现。`Dispatch wall`、query wait、readback wait 和 frame-fence wait 都是 CPU 侧开销，不计入 `Pass sum`。

| Algorithm | Pre-split leaf collect | Leaf error / frustum | Split candidate mark | Merge candidate score | Split / direct-diamond commit | Leaf-counter reset | Post-refine leaf collect | Mesh emit / draw args | Pass sum | Max pass sum |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| GPU ROAM-like | 0.0092 | 0.0077 | 0.0057 | 0.0070 | 0.0034 | 0.0029 | 0.0061 | 0.0156 | 0.0577 | 0.3152 |

## GPU 编排与渲染

| Algorithm | Snapshot build | Buffer allocation | Dispatch wall | Query wait | Readback wait | Frame fence wait | Render | Max render | Max upload B | Max readback B |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| Classic CPU ROAM | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0 | 0 |
| Data-Oriented CPU ROAM | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0 | 0 |
| GPU ROAM-like | 2.38 | 0.45 | 0.07 | 0.01 | 1.29 | 0.00 | 0.00 | 0.00 | 19076116 | 96 |
