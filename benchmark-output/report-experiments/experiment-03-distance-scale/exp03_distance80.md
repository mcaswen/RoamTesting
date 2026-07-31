# 运行时基准测试报告

- 相机路径：从地形边缘中点移动到地形中心
- 每种算法的运行时长：10 秒
- 详细 CSV：`runtime-benchmark-20260707-141710.csv`

- 构建配置：RelWithDebInfo
- Benchmark 标签：exp03_distance80
- GPU 设备：NVIDIA GeForce RTX 5090 D/PCIe/SSE2 (4.3.0 NVIDIA 591.86)
- GPU 计时模型：CPU DOD 拓扑基线加 GPU split-only、compaction、mesh emit 和 indirect draw；GPU ms 只统计 compute pass
- VSync：基准测试期间已关闭

- Height map：`assets/heightmaps/Hm_Terrain_Test_129.pgm` 129x129
- Terrain size：59.9
- Height scale：12
- Max depth 设置：20
- Distance scale：80
- Split/Merge 阈值：0.04 / 0.02

| Algorithm | Samples | Avg Frame ms | Max Frame ms | Avg LOD ms | Max LOD ms | Avg CPU Update ms | Avg CPU Upload ms | Avg GPU ms | Max GPU ms | Avg GPU Snapshot ms | Avg GPU Alloc ms | Avg GPU Dispatch Wall ms | Avg GPU Query Wait ms | Avg GPU Readback Wait ms | Avg Triangles | Max Triangles | Avg Nodes | Max Nodes | Avg CPU % | Max CPU % | Max Workers | Max Upload B | Max Readback B | Config Max Depth | Reached Max Depth | Max Topology Issues |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| Classic CPU ROAM | 371 | 27.00 | 41.19 | 25.29 | 39.19 | 20.25 | 0.64 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 31251.10 | 37136 | 62500.19 | 74270 | 98.81 | 250.52 | 1 | 0 | 0 | 20 | 17 | 0 |
| Data-Oriented CPU ROAM | 912 | 11.03 | 118.90 | 10.06 | 18.28 | 6.67 | 0.65 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 31463.34 | 37136 | 62924.68 | 74270 | 329.08 | 1856.09 | 8 | 0 | 0 | 20 | 17 | 0 |
| GPU ROAM-like | 719 | 13.96 | 33.78 | 13.65 | 21.06 | 5.27 | 0.87 | 0.65 | 2.92 | 1.72 | 0.00 | 0.25 | 0.00 | 1.98 | 31352.00 | 37136 | 62702.00 | 74270 | 198.82 | 1318.02 | 8 | 8318288 | 24 | 20 | 17 | 0 |
