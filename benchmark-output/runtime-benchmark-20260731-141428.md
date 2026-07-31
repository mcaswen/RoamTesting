# 运行时基准测试报告

- 相机路径：从地形边缘中点移动到地形中心
- 每种算法的采样时长：1 秒
- 详细 CSV：`runtime-benchmark-20260731-141428.csv`

- 构建配置：RelWithDebInfo
- 图形后端：OpenGL
- 图形适配器：NVIDIA GeForce RTX 5090 D/PCIe/SSE2 (4.3.0 NVIDIA 591.86)
- Benchmark 标签：phase-breakdown-opengl-final
- GPU 设备：NVIDIA GeForce RTX 5090 D/PCIe/SSE2 (4.3.0 NVIDIA 591.86)
- GPU 计时模型：CPU DOD 拓扑基线加有序 shader 阶段：初始 leaf compaction、error evaluation、candidate marking、split topology、active-leaf reset、最终 leaf compaction 和 mesh emit；indirect draw 单独统计
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
| Classic CPU ROAM | 116 | 8.63 | 28.24 | 8.01 | 24.24 | 12245.28 | 15239 | 43160.03 | 51340 | 96.27 | 538.33 | 1 | 14 | 14 | 0 |
| Data-Oriented CPU ROAM | 170 | 6.03 | 76.13 | 5.05 | 21.55 | 12211.59 | 15234 | 43390.53 | 51340 | 277.53 | 3667.30 | 8 | 14 | 14 | 0 |
| GPU ROAM-like | 121 | 8.52 | 26.42 | 8.07 | 25.30 | 12755.93 | 15242 | 41295.88 | 51340 | 225.94 | 1972.76 | 8 | 14 | 14 | 0 |

## CPU 实现阶段

`CPU update` 包含下表中互斥的算法阶段；`CPU upload` 是算法返回后的 renderer 上传。Classic 在扫描和弹出 split queue 时评估屏幕误差，因此单独的 `Error eval` 为零，这部分工作仍计入 `Split mark` / `Split topology`。

| Algorithm | CPU update | Prepare | Merge mark | Merge topology | Budget leaf collect | Error eval | Split mark | Split topology | Final leaf collect | Mesh emit | Validate | Finalize | CPU upload |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| Classic CPU ROAM | 7.69 | 0.23 | 0.76 | 0.04 | 0.10 | 0.00 | 0.50 | 0.10 | 0.11 | 4.66 | 0.00 | 1.19 | 0.25 |
| Data-Oriented CPU ROAM | 4.74 | 0.22 | 0.74 | 0.10 | 0.23 | 0.15 | 1.03 | 0.14 | 0.21 | 0.89 | 0.00 | 1.02 | 0.24 |
| GPU ROAM-like | 4.42 | 0.29 | 0.73 | 0.13 | 0.23 | 0.18 | 1.12 | 0.19 | 0.23 | 0.00 | 0.00 | 1.33 | 0.52 |

### 原生 pass 包络

这些是各实现原有的外围 pass 计时。它们与上方互斥阶段重叠，不能重复相加。

| Algorithm | Split | Merge | Emit | Validate |
| --- | ---: | ---: | ---: | ---: |
| Classic CPU ROAM | 0.61 | 0.79 | 4.77 | 0.00 |
| Data-Oriented CPU ROAM | 1.32 | 0.85 | 1.10 | 0.00 |
| GPU ROAM-like | 1.48 | 0.86 | 0.00 | 0.00 |

## GPU shader 阶段

每个数值都是一个有序 shader 阶段的延迟 GPU timestamp 结果。`Pass sum` 是七个互不重叠区间之和。`Dispatch wall`、query wait、readback wait 和 frame-fence wait 都是 CPU 侧开销，不计入 `Pass sum`。

| Algorithm | Initial leaf compact | Error evaluation | Candidate marking | Split topology | Active leaf reset | Final leaf compact | Mesh emit | Pass sum | Max pass sum |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| Classic CPU ROAM | 0.0000 | 0.0000 | 0.0000 | 0.0000 | 0.0000 | 0.0000 | 0.0000 | 0.0000 | 0.0000 |
| Data-Oriented CPU ROAM | 0.0000 | 0.0000 | 0.0000 | 0.0000 | 0.0000 | 0.0000 | 0.0000 | 0.0000 | 0.0000 |
| GPU ROAM-like | 0.0045 | 0.0046 | 0.0055 | 0.0037 | 0.0032 | 0.0038 | 0.0104 | 0.0356 | 0.0689 |

## GPU 编排与渲染

| Algorithm | Snapshot build | Buffer allocation | Dispatch wall | Query wait | Readback wait | Frame fence wait | Render | Max render | Max upload B | Max readback B |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| Classic CPU ROAM | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0 | 0 |
| Data-Oriented CPU ROAM | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0 | 0 |
| GPU ROAM-like | 1.11 | 0.00 | 0.13 | 0.00 | 1.38 | 0.00 | 0.00 | 0.00 | 5750132 | 88 |
