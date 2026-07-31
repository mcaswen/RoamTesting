# 运行时基准测试报告

- 相机路径：从地形边缘中点移动到地形中心
- 每种算法的运行时长：10 秒
- 详细 CSV：`runtime-benchmark-20260707-140821.csv`

- 构建配置：RelWithDebInfo
- Benchmark 标签：exp01_baseline_r01
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
| Classic CPU ROAM | 382 | 26.23 | 55.73 | 24.46 | 53.36 | 19.58 | 0.63 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 31243.83 | 37136 | 62485.65 | 74270 | 95.88 | 271.33 | 1 | 0 | 0 | 20 | 17 | 0 |
| Data-Oriented CPU ROAM | 804 | 12.51 | 88.50 | 11.66 | 21.02 | 8.32 | 0.63 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 31337.54 | 37136 | 62673.08 | 74270 | 337.72 | 1926.34 | 8 | 0 | 0 | 20 | 17 | 0 |
| GPU ROAM-like | 726 | 13.83 | 35.02 | 13.62 | 21.71 | 5.96 | 0.81 | 0.48 | 3.95 | 1.64 | 0.00 | 0.23 | 0.00 | 1.82 | 31628.76 | 37136 | 63255.53 | 74270 | 259.41 | 1385.33 | 8 | 8318288 | 24 | 20 | 17 | 0 |
