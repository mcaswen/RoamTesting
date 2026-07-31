# 运行时基准测试报告

- 相机路径：从地形边缘中点移动到地形中心
- 每种算法的运行时长：10 秒
- 详细 CSV：`runtime-benchmark-20260707-140914.csv`

- 构建配置：RelWithDebInfo
- Benchmark 标签：exp01_baseline_r02
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
| Classic CPU ROAM | 378 | 26.50 | 46.20 | 24.79 | 43.89 | 19.80 | 0.64 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 31118.11 | 37136 | 62234.21 | 74270 | 97.33 | 275.41 | 1 | 0 | 0 | 20 | 17 | 0 |
| Data-Oriented CPU ROAM | 912 | 11.03 | 88.48 | 10.07 | 19.39 | 6.78 | 0.65 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 31439.36 | 37136 | 62876.73 | 74270 | 307.04 | 1844.75 | 8 | 0 | 0 | 20 | 17 | 0 |
| GPU ROAM-like | 735 | 13.66 | 33.25 | 13.33 | 22.70 | 5.34 | 0.83 | 0.68 | 3.42 | 1.70 | 0.00 | 0.25 | 0.00 | 1.94 | 31395.94 | 37136 | 62789.88 | 74270 | 214.44 | 1259.00 | 8 | 8318288 | 24 | 20 | 17 | 0 |
