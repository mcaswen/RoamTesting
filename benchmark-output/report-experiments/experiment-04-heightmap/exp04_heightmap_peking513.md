# 运行时基准测试报告

- 相机路径：从地形边缘中点移动到地形中心
- 每种算法的运行时长：10 秒
- 详细 CSV：`runtime-benchmark-20260707-141847.csv`

- 构建配置：RelWithDebInfo
- Benchmark 标签：exp04_heightmap_peking513
- GPU 设备：NVIDIA GeForce RTX 5090 D/PCIe/SSE2 (4.3.0 NVIDIA 591.86)
- GPU 计时模型：CPU DOD 拓扑基线加 GPU split-only、compaction、mesh emit 和 indirect draw；GPU ms 只统计 compute pass
- VSync：基准测试期间已关闭

- Height map：`assets/heightmaps/Hm_Terrain_Peking_513.png` 547x547
- Terrain size：59.9
- Height scale：12
- Max depth 设置：20
- Distance scale：80
- Split/Merge 阈值：0.04 / 0.02

| Algorithm | Samples | Avg Frame ms | Max Frame ms | Avg LOD ms | Max LOD ms | Avg CPU Update ms | Avg CPU Upload ms | Avg GPU ms | Max GPU ms | Avg GPU Snapshot ms | Avg GPU Alloc ms | Avg GPU Dispatch Wall ms | Avg GPU Query Wait ms | Avg GPU Readback Wait ms | Avg Triangles | Max Triangles | Avg Nodes | Max Nodes | Avg CPU % | Max CPU % | Max Workers | Max Upload B | Max Readback B | Config Max Depth | Reached Max Depth | Max Topology Issues |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| Classic CPU ROAM | 1503 | 6.66 | 11.98 | 6.17 | 9.65 | 5.28 | 0.22 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 10798.52 | 12910 | 21595.65 | 25826 | 88.55 | 717.42 | 1 | 0 | 0 | 20 | 18 | 0 |
| Data-Oriented CPU ROAM | 2690 | 3.72 | 86.35 | 3.35 | 5.70 | 2.34 | 0.24 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 10824.17 | 12910 | 21646.96 | 25826 | 300.93 | 6341.03 | 8 | 0 | 0 | 20 | 18 | 0 |
| GPU ROAM-like | 2262 | 4.43 | 16.01 | 4.27 | 10.11 | 1.67 | 0.30 | 0.35 | 1.49 | 0.57 | 0.00 | 0.15 | 0.00 | 0.59 | 10834.63 | 12910 | 21667.90 | 25826 | 233.32 | 4460.30 | 8 | 2892560 | 24 | 20 | 18 | 0 |
