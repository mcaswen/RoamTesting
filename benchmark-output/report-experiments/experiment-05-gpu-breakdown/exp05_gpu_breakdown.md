# 运行时基准测试报告

- 相机路径：从地形边缘中点移动到地形中心
- 每种算法的运行时长：10 秒
- 详细 CSV：`runtime-benchmark-20260707-141936.csv`

- 构建配置：RelWithDebInfo
- Benchmark 标签：exp05_gpu_breakdown
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
| Classic CPU ROAM | 385 | 26.05 | 42.30 | 24.34 | 40.46 | 19.50 | 0.65 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 31221.42 | 37136 | 62440.84 | 74270 | 97.90 | 251.59 | 1 | 0 | 0 | 20 | 17 | 0 |
| Data-Oriented CPU ROAM | 908 | 11.07 | 87.59 | 10.12 | 18.57 | 6.76 | 0.64 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 31446.37 | 37136 | 62890.74 | 74270 | 330.83 | 1762.50 | 8 | 0 | 0 | 20 | 17 | 0 |
| GPU ROAM-like | 730 | 13.76 | 34.38 | 13.43 | 20.03 | 5.34 | 0.86 | 0.68 | 3.84 | 1.68 | 0.00 | 0.26 | 0.00 | 1.99 | 31301.19 | 37136 | 62600.38 | 74270 | 220.93 | 1550.27 | 8 | 8318288 | 24 | 20 | 17 | 0 |
