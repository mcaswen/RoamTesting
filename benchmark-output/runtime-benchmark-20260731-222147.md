# 运行时基准测试报告

- 相机路径：从地形边缘中点移动到地形中心
- 每种算法的采样时长：1 秒
- 详细 CSV：`runtime-benchmark-20260731-222147.csv`

- 构建配置：RelWithDebInfo
- 图形后端：D3D12
- 图形适配器：NVIDIA GeForce RTX 5090 D (Direct3D 12 (feature level 12_0); requested Agility SDK 614; runtime 6.2.26100.8737 (C:\WINDOWS\SYSTEM32\D3D12Core.dll); driver 32.0.15.9186)
- Benchmark 标签：roam-logical-stages-d3d12-final
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
| Classic CPU ROAM | 130 | 7.70 | 45.95 | 7.33 | 24.44 | 12037.44 | 15235 | 43976.28 | 51340 | 102.00 | 341.25 | 1 | 14 | 14 | 0 |
| Data-Oriented CPU ROAM | 212 | 4.83 | 21.64 | 4.53 | 19.07 | 12464.64 | 15237 | 42357.19 | 51340 | 376.10 | 3479.28 | 8 | 14 | 14 | 0 |
| GPU ROAM-like | 66 | 15.29 | 164.74 | 15.09 | 163.49 | 12541.74 | 15199 | 42908.09 | 51330 | 94.77 | 757.61 | 8 | 14 | 14 | 0 |

## ROAM 逻辑阶段对比

表中数值均为平均毫秒数。GPU ROAM-like 是混合路径：先运行完整的 CPU DOD 拓扑基线，再追加一轮 GPU split-only 细分和 GPU mesh emit。两个 GPU-like 列分别展示 CPU 与 shader 的重复职责，避免把 shader 链误解为完整的 GPU ROAM 实现。

| ROAM 逻辑阶段 | Classic CPU | DOD CPU | GPU-like CPU baseline | GPU-like shader | 阶段映射与限制 |
| --- | ---: | ---: | ---: | ---: | --- |
| Prepare / 帧状态 | 0.1492 | 0.1385 | 0.2419 | N/A | CPU 准备持久拓扑和 snapshot 输入 |
| Merge 候选评分 | 0.8333 | 0.7207 | 0.8318 | 0.0030 | Shader 为诊断重新扫描已 split 的父节点；GPU 不提交这些候选 |
| Merge 拓扑提交 / 向上级联 | 0.0335 | 0.0721 | 0.1904 | N/A | 持久 merge、邻接修复和级联回收只由 CPU DOD 执行 |
| Split 前 active leaf 收集 | 0.1591 | 0.2261 | 0.2146 | 0.0065 | GPU 在评分前再次压缩已上传的 CPU 拓扑 |
| 视点相关 leaf error / 视锥测试 | 0.0000 | 0.1526 | 0.1781 | 0.0035 | Classic 在 split 扫描中内联评估；GPU 基于 snapshot 重复执行 DOD 评分 |
| Split 候选标记 | 0.4696 | 0.9674 | 0.9267 | 0.0030 | 按 threshold 和 max depth 分类；GPU append 顺序不是误差优先级顺序 |
| Split 拓扑 / 裂缝约束提交 | 0.0750 | 0.0945 | 0.2769 | 0.0011 | GPU 只提交一轮受预算约束的细分，并只处理直接 base-neighbor diamond pair；没有递归 forced-split 链 |
| 细分后 active leaf 收集 | 0.1229 | 0.2150 | 0.2205 | 0.0028 | GPU 数值包含 counter reset 和 split 后 leaf compaction |
| Mesh emit / draw argument 生成 | 4.1912 | 0.8648 | 0.0000 | 0.0078 | GPU 输出非共享顶点、索引和 indirect draw argument |
| Finalize / 发布 packet | 1.1042 | 0.9060 | 1.2016 | N/A | CPU 发布统计和 renderer 资源契约 |

## CPU 实现阶段

`CPU update` 包含下表中互斥的算法阶段；`CPU upload` 是算法返回后的 renderer 上传。Classic 在扫描和弹出 split queue 时评估屏幕误差，因此单独的 `Error eval` 为零，这部分工作仍计入 `Split mark` / `Split topology`。

| Algorithm | CPU update | Prepare | Merge mark | Merge topology | Budget leaf collect | Error eval | Split mark | Split topology | Final leaf collect | Mesh emit | Finalize | CPU upload |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| Classic CPU ROAM | 7.1386 | 0.1492 | 0.8333 | 0.0335 | 0.1591 | 0.0000 | 0.4696 | 0.0750 | 0.1229 | 4.1912 | 1.1042 | 0.1374 |
| Data-Oriented CPU ROAM | 4.3579 | 0.1385 | 0.7207 | 0.0721 | 0.2261 | 0.1526 | 0.9674 | 0.0945 | 0.2150 | 0.8648 | 0.9060 | 0.1173 |
| GPU ROAM-like | 4.2826 | 0.2419 | 0.8318 | 0.1904 | 0.2146 | 0.1781 | 0.9267 | 0.2769 | 0.2205 | 0.0000 | 1.2016 | 0.0000 |

### 原生 pass 包络

这些是各实现原有的外围 pass 计时。它们与上方互斥阶段重叠，不能重复相加。

| Algorithm | Split | Merge | Emit | Validate |
| --- | ---: | ---: | ---: | ---: |
| Classic CPU ROAM | 0.5449 | 0.8670 | 4.3141 | 0.0000 |
| Data-Oriented CPU ROAM | 1.2144 | 0.7928 | 1.0798 | 0.0000 |
| GPU ROAM-like | 1.3816 | 1.0222 | 0.0000 | 0.0000 |

## GPU shader dispatch 明细

每个数值都是一个物理 dispatch 的延迟 GPU timestamp 结果。`Pass sum` 是八个互不重叠区间之和。本表用于解释上方 GPU-like shader 列，不能据此认定所有 ROAM 阶段都已在 GPU 实现。`Dispatch wall`、query wait、readback wait 和 frame-fence wait 都是 CPU 侧开销，不计入 `Pass sum`。

| Algorithm | Pre-split leaf collect | Leaf error / frustum | Split candidate mark | Merge candidate score | Split / direct-diamond commit | Leaf-counter reset | Post-refine leaf collect | Mesh emit / draw args | Pass sum | Max pass sum |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| GPU ROAM-like | 0.0065 | 0.0035 | 0.0030 | 0.0030 | 0.0011 | 0.0008 | 0.0020 | 0.0078 | 0.0276 | 0.0432 |

## GPU 编排与渲染

| Algorithm | Snapshot build | Buffer allocation | Dispatch wall | Query wait | Readback wait | Frame fence wait | Render | Max render | Max upload B | Max readback B |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| Classic CPU ROAM | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.01 | 0.01 | 0 | 0 |
| Data-Oriented CPU ROAM | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.01 | 0.02 | 0 | 0 |
| GPU ROAM-like | 1.05 | 6.98 | 0.51 | 0.00 | 0.00 | 0.00 | 0.06 | 0.10 | 5748992 | 104 |
