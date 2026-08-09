# 运行时基准测试报告

- 相机路径：从地形边缘中点移动到地形中心
- 每种算法的采样时长：1 秒
- 详细 CSV：`runtime-benchmark-20260731-222129.csv`

- 构建配置：RelWithDebInfo
- 图形后端：OpenGL
- 图形适配器：NVIDIA GeForce RTX 5090 D/PCIe/SSE2 (4.3.0 NVIDIA 591.86)
- Benchmark 标签：roam-logical-stages-opengl-final
- GPU 设备：NVIDIA GeForce RTX 5090 D/PCIe/SSE2 (4.3.0 NVIDIA 591.86)
- GPU 计时模型：CPU DOD 拓扑基线加有序 shader 阶段：split 前 leaf 收集、leaf error 评估、split 候选标记、诊断性 merge 候选评分、split/direct-diamond 拓扑提交、细分后 leaf 收集和 mesh emit；GPU merge 拓扑尚未实现；indirect draw 单独统计
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
| Classic CPU ROAM | 121 | 8.27 | 27.22 | 7.73 | 23.08 | 12136.73 | 15242 | 43518.98 | 51340 | 87.63 | 658.63 | 1 | 14 | 14 | 0 |
| Data-Oriented CPU ROAM | 188 | 5.45 | 75.33 | 4.62 | 18.86 | 12166.96 | 15226 | 43528.12 | 51340 | 227.17 | 3728.00 | 8 | 14 | 14 | 0 |
| GPU ROAM-like | 138 | 7.44 | 24.03 | 7.09 | 22.67 | 12605.94 | 15241 | 41745.52 | 51340 | 237.00 | 2420.65 | 8 | 14 | 14 | 0 |

## ROAM 逻辑阶段对比

表中数值均为平均毫秒数。GPU ROAM-like 是混合路径：先运行完整的 CPU DOD 拓扑基线，再追加一轮 GPU split-only 细分和 GPU mesh emit。两个 GPU-like 列分别展示 CPU 与 shader 的重复职责，避免把 shader 链误解为完整的 GPU ROAM 实现。

| ROAM 逻辑阶段 | Classic CPU | DOD CPU | GPU-like CPU baseline | GPU-like shader | 阶段映射与限制 |
| --- | ---: | ---: | ---: | ---: | --- |
| Prepare / 帧状态 | 0.1653 | 0.1708 | 0.1976 | N/A | CPU 准备持久拓扑和 snapshot 输入 |
| Merge 候选评分 | 0.8796 | 0.6829 | 0.7180 | 0.0055 | Shader 为诊断重新扫描已 split 的父节点；GPU 不提交这些候选 |
| Merge 拓扑提交 / 向上级联 | 0.0357 | 0.0841 | 0.0995 | N/A | 持久 merge、邻接修复和级联回收只由 CPU DOD 执行 |
| Split 前 active leaf 收集 | 0.1533 | 0.2218 | 0.2149 | 0.0052 | GPU 在评分前再次压缩已上传的 CPU 拓扑 |
| 视点相关 leaf error / 视锥测试 | 0.0000 | 0.1289 | 0.1345 | 0.0063 | Classic 在 split 扫描中内联评估；GPU 基于 snapshot 重复执行 DOD 评分 |
| Split 候选标记 | 0.4868 | 1.0283 | 1.0323 | 0.0048 | 按 threshold 和 max depth 分类；GPU append 顺序不是误差优先级顺序 |
| Split 拓扑 / 裂缝约束提交 | 0.0895 | 0.1103 | 0.1517 | 0.0033 | GPU 只提交一轮受预算约束的细分，并只处理直接 base-neighbor diamond pair；没有递归 forced-split 链 |
| 细分后 active leaf 收集 | 0.1131 | 0.2065 | 0.2172 | 0.0073 | GPU 数值包含 counter reset 和 split 后 leaf compaction |
| Mesh emit / draw argument 生成 | 4.3027 | 0.7322 | 0.0000 | 0.0110 | GPU 输出非共享顶点、索引和 indirect draw argument |
| Finalize / 发布 packet | 1.2236 | 0.9851 | 1.1158 | N/A | CPU 发布统计和 renderer 资源契约 |

## CPU 实现阶段

`CPU update` 包含下表中互斥的算法阶段；`CPU upload` 是算法返回后的 renderer 上传。Classic 在扫描和弹出 split queue 时评估屏幕误差，因此单独的 `Error eval` 为零，这部分工作仍计入 `Split mark` / `Split topology`。

| Algorithm | CPU update | Prepare | Merge mark | Merge topology | Budget leaf collect | Error eval | Split mark | Split topology | Final leaf collect | Mesh emit | Finalize | CPU upload |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| Classic CPU ROAM | 7.4504 | 0.1653 | 0.8796 | 0.0357 | 0.1533 | 0.0000 | 0.4868 | 0.0895 | 0.1131 | 4.3027 | 1.2236 | 0.2161 |
| Data-Oriented CPU ROAM | 4.3511 | 0.1708 | 0.6829 | 0.0841 | 0.2218 | 0.1289 | 1.0283 | 0.1103 | 0.2065 | 0.7322 | 0.9851 | 0.2142 |
| GPU ROAM-like | 3.8817 | 0.1976 | 0.7180 | 0.0995 | 0.2149 | 0.1345 | 1.0323 | 0.1517 | 0.2172 | 0.0000 | 1.1158 | 0.4676 |

### 原生 pass 包络

这些是各实现原有的外围 pass 计时。它们与上方互斥阶段重叠，不能重复相加。

| Algorithm | Split | Merge | Emit | Validate |
| --- | ---: | ---: | ---: | ---: |
| Classic CPU ROAM | 0.5766 | 0.9156 | 4.4159 | 0.0000 |
| Data-Oriented CPU ROAM | 1.2675 | 0.7670 | 0.9387 | 0.0000 |
| GPU ROAM-like | 1.3185 | 0.8175 | 0.0000 | 0.0000 |

## GPU shader dispatch 明细

每个数值都是一个物理 dispatch 的延迟 GPU timestamp 结果。`Pass sum` 是八个互不重叠区间之和。本表用于解释上方 GPU-like shader 列，不能据此认定所有 ROAM 阶段都已在 GPU 实现。`Dispatch wall`、query wait、readback wait 和 frame-fence wait 都是 CPU 侧开销，不计入 `Pass sum`。

| Algorithm | Pre-split leaf collect | Leaf error / frustum | Split candidate mark | Merge candidate score | Split / direct-diamond commit | Leaf-counter reset | Post-refine leaf collect | Mesh emit / draw args | Pass sum | Max pass sum |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| GPU ROAM-like | 0.0052 | 0.0063 | 0.0048 | 0.0055 | 0.0033 | 0.0029 | 0.0044 | 0.0110 | 0.0434 | 0.0786 |

## GPU 编排与渲染

| Algorithm | Snapshot build | Buffer allocation | Dispatch wall | Query wait | Readback wait | Frame fence wait | Render | Max render | Max upload B | Max readback B |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| Classic CPU ROAM | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0 | 0 |
| Data-Oriented CPU ROAM | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0 | 0 |
| GPU ROAM-like | 0.96 | 0.01 | 0.09 | 0.00 | 1.34 | 0.00 | 0.00 | 0.00 | 5750132 | 96 |
