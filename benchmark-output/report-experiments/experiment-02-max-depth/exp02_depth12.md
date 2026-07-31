# 运行时基准测试报告

- 相机路径：从地形边缘中点移动到地形中心
- 每种算法的运行时长：10 秒
- 详细 CSV：`runtime-benchmark-20260707-141051.csv`

- 构建配置：RelWithDebInfo
- Benchmark 标签：exp02_depth12
- GPU 设备：NVIDIA GeForce RTX 5090 D/PCIe/SSE2 (4.3.0 NVIDIA 591.86)
- GPU 计时模型：CPU DOD 拓扑基线加 GPU split-only、compaction、mesh emit 和 indirect draw；GPU ms 只统计 compute pass
- VSync：基准测试期间已关闭

- Height map：`assets/heightmaps/Hm_Terrain_Test_129.pgm` 129x129
- Terrain size：59.9
- Height scale：12
- Max depth 设置：12
- Distance scale：80
- Split/Merge 阈值：0.04 / 0.02

| Algorithm | Samples | Avg Frame ms | Max Frame ms | Avg LOD ms | Max LOD ms | Avg CPU Update ms | Avg CPU Upload ms | Avg GPU ms | Max GPU ms | Avg GPU Snapshot ms | Avg GPU Alloc ms | Avg GPU Dispatch Wall ms | Avg GPU Query Wait ms | Avg GPU Readback Wait ms | Avg Triangles | Max Triangles | Avg Nodes | Max Nodes | Avg CPU % | Max CPU % | Max Workers | Max Upload B | Max Readback B | Config Max Depth | Reached Max Depth | Max Topology Issues |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| Classic CPU ROAM | 2503 | 4.00 | 9.39 | 3.65 | 7.31 | 3.08 | 0.17 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 8116.32 | 8176 | 16230.64 | 16350 | 77.15 | 1000.10 | 1 | 0 | 0 | 12 | 12 | 0 |
| Data-Oriented CPU ROAM | 3958 | 2.53 | 84.79 | 2.24 | 5.86 | 1.61 | 0.18 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 8117.30 | 8176 | 16232.60 | 16350 | 308.21 | 7257.69 | 8 | 0 | 0 | 12 | 12 | 0 |
| GPU ROAM-like | 3096 | 3.23 | 10.24 | 3.11 | 6.70 | 1.23 | 0.24 | 0.30 | 1.29 | 0.44 | 0.00 | 0.17 | 0.00 | 0.38 | 8116.34 | 8176 | 16230.69 | 16350 | 222.89 | 6031.52 | 8 | 1831248 | 24 | 12 | 12 | 0 |
