# 运行时基准测试报告

- 相机路径：从地形边缘中点移动到地形中心
- 每种算法的运行时长：10 秒
- 详细 CSV：`runtime-benchmark-20260707-141311.csv`

- 构建配置：RelWithDebInfo
- Benchmark 标签：exp02_depth18
- GPU 设备：NVIDIA GeForce RTX 5090 D/PCIe/SSE2 (4.3.0 NVIDIA 591.86)
- GPU 计时模型：CPU DOD 拓扑基线加 GPU split-only、compaction、mesh emit 和 indirect draw；GPU ms 只统计 compute pass
- VSync：基准测试期间已关闭

- Height map：`assets/heightmaps/Hm_Terrain_Test_129.pgm` 129x129
- Terrain size：59.9
- Height scale：12
- Max depth 设置：18
- Distance scale：80
- Split/Merge 阈值：0.04 / 0.02

| Algorithm | Samples | Avg Frame ms | Max Frame ms | Avg LOD ms | Max LOD ms | Avg CPU Update ms | Avg CPU Upload ms | Avg GPU ms | Max GPU ms | Avg GPU Snapshot ms | Avg GPU Alloc ms | Avg GPU Dispatch Wall ms | Avg GPU Query Wait ms | Avg GPU Readback Wait ms | Avg Triangles | Max Triangles | Avg Nodes | Max Nodes | Avg CPU % | Max CPU % | Max Workers | Max Upload B | Max Readback B | Config Max Depth | Reached Max Depth | Max Topology Issues |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| Classic CPU ROAM | 370 | 27.05 | 43.33 | 25.27 | 39.47 | 20.09 | 0.66 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 31315.49 | 37136 | 62628.99 | 74270 | 99.37 | 234.80 | 1 | 0 | 0 | 18 | 17 | 0 |
| Data-Oriented CPU ROAM | 925 | 10.88 | 97.06 | 9.90 | 19.06 | 6.66 | 0.64 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 31446.86 | 37136 | 62891.71 | 74270 | 310.23 | 1931.78 | 8 | 0 | 0 | 18 | 17 | 0 |
| GPU ROAM-like | 721 | 13.93 | 34.15 | 13.59 | 24.33 | 5.42 | 0.83 | 0.68 | 2.09 | 1.71 | 0.00 | 0.27 | 0.00 | 1.98 | 31378.00 | 37136 | 62754.00 | 74270 | 223.66 | 1301.64 | 8 | 8318288 | 24 | 18 | 17 | 0 |
