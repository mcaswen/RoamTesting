# 运行时基准测试报告

- 相机路径：从地形边缘中点移动到地形中心
- 每种算法的采样时长：1 秒
- 详细 CSV：`runtime-benchmark-20260731-140624.csv`

- 构建配置：RelWithDebInfo
- 图形后端：D3D12
- 图形适配器：NVIDIA GeForce RTX 5090 D (Direct3D 12 (feature level 12_0); requested Agility SDK 614; runtime 6.2.26100.8737 (C:\WINDOWS\SYSTEM32\D3D12Core.dll); driver 32.0.15.9186)
- Benchmark 标签：phase-breakdown-validation
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
| Classic CPU ROAM | 109 | 9.23 | 51.97 | 8.60 | 26.13 | 12108.53 | 15217 | 43613.19 | 51340 | 98.83 | 331.64 | 1 | 14 | 14 | 0 |
| Data-Oriented CPU ROAM | 178 | 5.76 | 24.10 | 5.32 | 20.56 | 12503.04 | 15244 | 42221.65 | 51340 | 273.56 | 3013.67 | 8 | 14 | 14 | 0 |
| GPU ROAM-like | 63 | 16.11 | 184.47 | 15.72 | 183.27 | 12307.05 | 15199 | 43758.98 | 51324 | 87.03 | 885.72 | 8 | 14 | 14 | 0 |

## CPU 实现阶段

`CPU update` 包含下表中互斥的算法阶段；`CPU upload` 是算法返回后的 renderer 上传。Classic 在扫描和弹出 split queue 时评估屏幕误差，因此单独的 `Error eval` 为零，这部分工作仍计入 `Split mark` / `Split topology`。

| Algorithm | CPU update | Prepare | Merge mark | Merge topology | Budget leaf collect | Error eval | Split mark | Split topology | Final leaf collect | Mesh emit | Validate | Finalize | CPU upload |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| Classic CPU ROAM | 8.32 | 0.20 | 0.88 | 0.04 | 0.12 | 0.00 | 0.51 | 0.11 | 0.12 | 4.84 | 0.00 | 1.49 | 0.16 |
| Data-Oriented CPU ROAM | 5.09 | 0.17 | 0.83 | 0.10 | 0.24 | 0.18 | 1.08 | 0.14 | 0.22 | 1.02 | 0.00 | 1.11 | 0.15 |
| GPU ROAM-like | 5.47 | 0.48 | 1.01 | 0.25 | 0.22 | 0.20 | 1.16 | 0.37 | 0.23 | 0.00 | 0.00 | 1.56 | 0.00 |

### 原生 pass 包络

这些是各实现原有的外围 pass 计时。它们与上方互斥阶段重叠，不能重复相加。

| Algorithm | Split | Merge | Emit | Validate |
| --- | ---: | ---: | ---: | ---: |
| Classic CPU ROAM | 0.62 | 0.92 | 4.97 | 0.00 |
| Data-Oriented CPU ROAM | 1.40 | 0.93 | 1.24 | 0.00 |
| GPU ROAM-like | 1.72 | 1.26 | 0.00 | 0.00 |

## GPU shader 阶段

每个数值都是一个有序 shader 阶段的延迟 GPU timestamp 结果。`Pass sum` 是七个互不重叠区间之和。`Dispatch wall`、query wait、readback wait 和 frame-fence wait 都是 CPU 侧开销，不计入 `Pass sum`。

| Algorithm | Initial leaf compact | Error evaluation | Candidate marking | Split topology | Active leaf reset | Final leaf compact | Mesh emit | Pass sum | Max pass sum |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| Classic CPU ROAM | 0.0000 | 0.0000 | 0.0000 | 0.0000 | 0.0000 | 0.0000 | 0.0000 | 0.0000 | 0.0000 |
| Data-Oriented CPU ROAM | 0.0000 | 0.0000 | 0.0000 | 0.0000 | 0.0000 | 0.0000 | 0.0000 | 0.0000 | 0.0000 |
| GPU ROAM-like | 0.0043 | 0.0028 | 0.0032 | 0.0012 | 0.0009 | 0.0021 | 0.0079 | 0.0223 | 0.0250 |

## GPU 编排与渲染

| Algorithm | Snapshot build | Buffer allocation | Dispatch wall | Query wait | Readback wait | Frame fence wait | Render | Max render | Max upload B | Max readback B |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| Classic CPU ROAM | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.01 | 0.01 | 0 | 0 |
| Data-Oriented CPU ROAM | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.01 | 0.01 | 0 | 0 |
| GPU ROAM-like | 1.17 | 6.06 | 0.34 | 0.00 | 0.00 | 0.00 | 0.05 | 0.06 | 5748320 | 96 |
