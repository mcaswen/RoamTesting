# 运行时基准测试报告

- 相机路径：从地形边缘中点移动到地形中心
- 每种算法的采样时长：1 秒
- 详细 CSV：`runtime-benchmark-20260731-141441.csv`

- 构建配置：RelWithDebInfo
- 图形后端：D3D12
- 图形适配器：NVIDIA GeForce RTX 5090 D (Direct3D 12 (feature level 12_0); requested Agility SDK 614; runtime 6.2.26100.8737 (C:\WINDOWS\SYSTEM32\D3D12Core.dll); driver 32.0.15.9186)
- Benchmark 标签：phase-breakdown-d3d12-final
- GPU 计时模型：有序 DX12 shader 阶段包括初始 leaf compaction、error evaluation、candidate marking、split topology、active-leaf reset、最终 leaf compaction 和 mesh emit；ExecuteIndirect 单独统计
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
| Classic CPU ROAM | 118 | 8.48 | 49.93 | 7.94 | 25.88 | 12157.96 | 15213 | 43568.22 | 51340 | 101.01 | 326.31 | 1 | 14 | 14 | 0 |
| Data-Oriented CPU ROAM | 193 | 5.32 | 25.34 | 4.91 | 21.48 | 12460.43 | 15237 | 42388.59 | 51340 | 261.85 | 3289.13 | 8 | 14 | 14 | 0 |
| GPU ROAM-like | 65 | 15.50 | 180.29 | 15.15 | 179.05 | 12427.51 | 15199 | 43379.14 | 51324 | 114.44 | 963.13 | 8 | 14 | 14 | 0 |

## CPU 实现阶段

`CPU update` 包含下表中互斥的算法阶段；`CPU upload` 是算法返回后的 renderer 上传。Classic 在扫描和弹出 split queue 时评估屏幕误差，因此单独的 `Error eval` 为零，这部分工作仍计入 `Split mark` / `Split topology`。

| Algorithm | CPU update | Prepare | Merge mark | Merge topology | Budget leaf collect | Error eval | Split mark | Split topology | Final leaf collect | Mesh emit | Validate | Finalize | CPU upload |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| Classic CPU ROAM | 7.71 | 0.19 | 0.83 | 0.04 | 0.13 | 0.00 | 0.53 | 0.10 | 0.12 | 4.51 | 0.00 | 1.26 | 0.15 |
| Data-Oriented CPU ROAM | 4.69 | 0.15 | 0.73 | 0.09 | 0.23 | 0.16 | 1.00 | 0.13 | 0.22 | 0.95 | 0.00 | 1.02 | 0.14 |
| GPU ROAM-like | 4.82 | 0.39 | 0.81 | 0.22 | 0.22 | 0.19 | 1.02 | 0.36 | 0.22 | 0.00 | 0.00 | 1.39 | 0.00 |

### 原生 pass 包络

这些是各实现原有的外围 pass 计时。它们与上方互斥阶段重叠，不能重复相加。

| Algorithm | Split | Merge | Emit | Validate |
| --- | ---: | ---: | ---: | ---: |
| Classic CPU ROAM | 0.63 | 0.87 | 4.63 | 0.00 |
| Data-Oriented CPU ROAM | 1.29 | 0.82 | 1.17 | 0.00 |
| GPU ROAM-like | 1.57 | 1.03 | 0.00 | 0.00 |

## GPU shader 阶段

每个数值都是一个有序 shader 阶段的延迟 GPU timestamp 结果。`Pass sum` 是七个互不重叠区间之和。`Dispatch wall`、query wait、readback wait 和 frame-fence wait 都是 CPU 侧开销，不计入 `Pass sum`。

| Algorithm | Initial leaf compact | Error evaluation | Candidate marking | Split topology | Active leaf reset | Final leaf compact | Mesh emit | Pass sum | Max pass sum |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| Classic CPU ROAM | 0.0000 | 0.0000 | 0.0000 | 0.0000 | 0.0000 | 0.0000 | 0.0000 | 0.0000 | 0.0000 |
| Data-Oriented CPU ROAM | 0.0000 | 0.0000 | 0.0000 | 0.0000 | 0.0000 | 0.0000 | 0.0000 | 0.0000 | 0.0000 |
| GPU ROAM-like | 0.0040 | 0.0026 | 0.0031 | 0.0012 | 0.0009 | 0.0021 | 0.0078 | 0.0217 | 0.0249 |

## GPU 编排与渲染

| Algorithm | Snapshot build | Buffer allocation | Dispatch wall | Query wait | Readback wait | Frame fence wait | Render | Max render | Max upload B | Max readback B |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| Classic CPU ROAM | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.01 | 0.01 | 0 | 0 |
| Data-Oriented CPU ROAM | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.01 | 0.01 | 0 | 0 |
| GPU ROAM-like | 1.19 | 6.27 | 0.34 | 0.00 | 0.00 | 0.00 | 0.05 | 0.25 | 5748320 | 96 |
