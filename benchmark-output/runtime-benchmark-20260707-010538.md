# 运行时基准测试报告

- 相机路径：从地形边缘中点移动到地形中心
- 每种算法的运行时长：10 秒
- 详细 CSV：`runtime-benchmark-20260707-010538.csv`

- 构建配置：RelWithDebInfo
- GPU 设备：NVIDIA GeForce RTX 5090 D/PCIe/SSE2 (4.3.0 NVIDIA 591.86)
- GPU 计时模型：CPU DOD 拓扑基线加 GPU split-only、compaction、mesh emit 和 indirect draw；GPU ms 只统计 compute pass
- VSync：基准测试期间已关闭

- Height map：`assets/heightmaps/Hm_Terrain_Test_129.pgm` 129x129
- Terrain size：80
- Height scale：12
- Max depth 设置：20
- Distance scale：80
- Split/Merge 阈值：0.04 / 0.02

| Algorithm | Samples | Avg Frame ms | Max Frame ms | Avg LOD ms | Max LOD ms | Avg CPU Update ms | Avg CPU Upload ms | Avg GPU ms | Max GPU ms | Avg Triangles | Max Triangles | Avg Nodes | Max Nodes | Avg CPU % | Max CPU % | Max Workers | Max Upload B | Max Readback B | Config Max Depth | Reached Max Depth | Max Topology Issues |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| Classic CPU ROAM | 575 | 17.40 | 29.59 | 16.94 | 29.09 | 13.53 | 0.85 | 0.00 | 0.00 | 26301.67 | 33326 | 52601.34 | 66650 | 99.85 | 107.03 | 1 | 0 | 0 | 20 | 17 | 0 |
| Data-Oriented CPU ROAM | 1203 | 8.35 | 39.21 | 8.04 | 13.19 | 5.46 | 0.76 | 0.00 | 0.00 | 26503.31 | 33326 | 53004.61 | 66650 | 99.99 | 116.98 | 8 | 0 | 0 | 20 | 17 | 0 |
| GPU ROAM-like | 888 | 11.30 | 30.10 | 10.88 | 16.03 | 3.90 | 1.14 | 0.19 | 2.03 | 26511.34 | 33328 | 53020.69 | 66654 | 100.01 | 111.97 | 8 | 7531412 | 88 | 20 | 17 | 0 |
