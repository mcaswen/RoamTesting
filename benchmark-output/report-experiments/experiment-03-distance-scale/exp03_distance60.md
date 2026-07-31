# 运行时基准测试报告

- 相机路径：从地形边缘中点移动到地形中心
- 每种算法的运行时长：10 秒
- 详细 CSV：`runtime-benchmark-20260707-141623.csv`

- 构建配置：RelWithDebInfo
- Benchmark 标签：exp03_distance60
- GPU 设备：NVIDIA GeForce RTX 5090 D/PCIe/SSE2 (4.3.0 NVIDIA 591.86)
- GPU 计时模型：CPU DOD 拓扑基线加 GPU split-only、compaction、mesh emit 和 indirect draw；GPU ms 只统计 compute pass
- VSync：基准测试期间已关闭

- Height map：`assets/heightmaps/Hm_Terrain_Test_129.pgm` 129x129
- Terrain size：59.9
- Height scale：12
- Max depth 设置：20
- Distance scale：60
- Split/Merge 阈值：0.04 / 0.02

| Algorithm | Samples | Avg Frame ms | Max Frame ms | Avg LOD ms | Max LOD ms | Avg CPU Update ms | Avg CPU Upload ms | Avg GPU ms | Max GPU ms | Avg GPU Snapshot ms | Avg GPU Alloc ms | Avg GPU Dispatch Wall ms | Avg GPU Query Wait ms | Avg GPU Readback Wait ms | Avg Triangles | Max Triangles | Avg Nodes | Max Nodes | Avg CPU % | Max CPU % | Max Workers | Max Upload B | Max Readback B | Config Max Depth | Reached Max Depth | Max Topology Issues |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| Classic CPU ROAM | 501 | 19.97 | 33.86 | 18.53 | 32.13 | 15.01 | 0.52 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 24997.80 | 31328 | 49993.60 | 62654 | 97.08 | 283.59 | 1 | 0 | 0 | 20 | 15 | 0 |
| Data-Oriented CPU ROAM | 1205 | 8.33 | 89.35 | 7.57 | 14.52 | 5.24 | 0.51 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 25192.88 | 31328 | 50383.76 | 62654 | 313.19 | 2474.18 | 8 | 0 | 0 | 20 | 15 | 0 |
| GPU ROAM-like | 965 | 10.40 | 27.89 | 10.11 | 16.97 | 3.92 | 0.67 | 0.59 | 1.92 | 1.31 | 0.00 | 0.25 | 0.00 | 1.60 | 25041.73 | 31328 | 50081.46 | 62654 | 217.84 | 1859.95 | 8 | 7017296 | 24 | 20 | 15 | 0 |
