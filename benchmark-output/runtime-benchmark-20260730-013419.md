# 运行时基准测试报告

- 相机路径：从地形边缘中点移动到地形中心
- 每种算法的采样时长：1 秒
- 详细 CSV：`runtime-benchmark-20260730-013419.csv`

- 构建配置：RelWithDebInfo
- 图形后端：D3D12
- 图形适配器：NVIDIA GeForce RTX 5090 D (Direct3D 12 (feature level 12_0); requested Agility SDK 614; runtime 6.2.26100.8737 (C:\WINDOWS\SYSTEM32\D3D12Core.dll); driver 32.0.15.9186)
- GPU 计时模型：DX12 compute 执行拓扑细分和 GPU mesh emit，并使用 ExecuteIndirect
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
| Classic CPU ROAM | 147 | 6.83 | 43.94 | 6.46 | 22.41 | 12234.93 | 15235 | 43265.33 | 51340 | 96.59 | 343.84 | 1 | 14 | 14 | 0 |
| Data-Oriented CPU ROAM | 213 | 4.81 | 22.24 | 4.51 | 18.81 | 12468.60 | 15235 | 42358.23 | 51340 | 252.02 | 3326.50 | 8 | 14 | 14 | 0 |
| GPU ROAM-like | 89 | 11.40 | 159.77 | 11.09 | 158.76 | 12374.03 | 15199 | 43364.13 | 51336 | 112.95 | 1554.55 | 8 | 14 | 14 | 0 |

## CPU 算法阶段

`CPU update` 是外围墙钟时间区间。其他列是可能重叠的诊断子阶段：`Topology` 包含 split 和 merge，`Decision` 包含候选收集与 split 决策。

| Algorithm | CPU update | Error eval | Decision | Topology | Collect | Mesh build | Split | Merge | Emit | Validate | CPU upload |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| Classic CPU ROAM | 5.60 | 0.00 | 0.45 | 1.05 | 0.00 | 4.30 | 0.45 | 0.59 | 4.30 | 0.00 | 0.13 |
| Data-Oriented CPU ROAM | 3.58 | 0.15 | 0.09 | 0.17 | 1.71 | 1.05 | 1.23 | 0.79 | 1.05 | 0.00 | 0.12 |
| GPU ROAM-like | 2.95 | 0.17 | 0.00 | 0.00 | 1.68 | 0.00 | 1.30 | 0.88 | 0.00 | 0.00 | 0.00 |

## GPU shader 阶段

每个数值都是一个有序 shader 阶段的延迟 GPU timestamp 结果。`Pass sum` 是七个互不重叠区间之和。`Dispatch wall`、query wait、readback wait 和 frame-fence wait 都是 CPU 侧开销，不计入 `Pass sum`。

| Algorithm | Initial leaf compact | Error evaluation | Candidate marking | Split topology | Active leaf reset | Final leaf compact | Mesh emit | Pass sum | Max pass sum |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| Classic CPU ROAM | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 |
| Data-Oriented CPU ROAM | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 |
| GPU ROAM-like | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.01 | 0.02 | 0.02 |

## GPU 编排与渲染

| Algorithm | Snapshot build | Buffer allocation | Dispatch wall | Query wait | Readback wait | Frame fence wait | Render | Max render | Max upload B | Max readback B |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| Classic CPU ROAM | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.01 | 0.01 | 0 | 0 |
| Data-Oriented CPU ROAM | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.01 | 0.01 | 0 | 0 |
| GPU ROAM-like | 1.04 | 4.43 | 0.25 | 0.00 | 0.00 | 0.00 | 0.05 | 0.06 | 5749664 | 96 |
