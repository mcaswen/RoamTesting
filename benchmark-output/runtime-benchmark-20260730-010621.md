# 运行时基准测试报告

- 相机路径：从地形边缘中点移动到地形中心
- 每种算法的采样时长：10 秒
- 详细 CSV：`runtime-benchmark-20260730-010621.csv`

- 构建配置：RelWithDebInfo
- 图形后端：OpenGL
- 图形适配器：NVIDIA GeForce RTX 5090 D/PCIe/SSE2 (4.3.0 NVIDIA 591.86)
- GPU 设备：NVIDIA GeForce RTX 5090 D/PCIe/SSE2 (4.3.0 NVIDIA 591.86)
- GPU 计时模型：CPU DOD 拓扑基线加 GPU split-only、compaction、mesh emit 和 indirect draw；GPU ms 只统计 compute pass
- VSync：基准测试期间已关闭

- Height map：`assets/heightmaps/Hm_Terrain_Test_129.pgm` 129x129
- Terrain size：30
- Height scale：4
- Max depth 设置：20
- ROAM 屏幕空间 split/merge 阈值：2.53 px / 1.11 px
- ROAM triangle budget：200000

## 总体结果

| Algorithm | Samples | Avg Frame ms | Max Frame ms | Avg LOD ms | Max LOD ms | Avg CPU Update ms | Avg CPU Upload ms | Avg GPU ms | Max GPU ms | Avg GPU Snapshot ms | Avg GPU Alloc ms | Avg GPU Dispatch Wall ms | Avg GPU Query Wait ms | Avg GPU Readback Wait ms | Avg Frame Fence Wait ms | Max Frame Fence Wait ms | Avg Render ms | Max Render ms | Avg Triangles | Max Triangles | Avg Nodes | Max Nodes | Avg CPU % | Max CPU % | Max Workers | Max Upload B | Max Readback B | Config Max Depth | Reached Max Depth | Max Topology Issues |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| Classic CPU ROAM | 59 | 170.88 | 641.55 | 172.03 | 597.87 | 138.99 | 1.34 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 127865.81 | 134995 | 686829.90 | 1070702 | 100.06 | 109.93 | 1 | 0 | 0 | 20 | 20 | 0 |
| Data-Oriented CPU ROAM | 137 | 78.39 | 718.62 | 73.05 | 556.11 | 50.07 | 1.32 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 128412.14 | 135045 | 675011.78 | 1091102 | 277.71 | 625.47 | 8 | 0 | 0 | 20 | 20 | 0 |
| GPU ROAM-like | 94 | 112.96 | 605.96 | 107.26 | 553.75 | 48.28 | 11.89 | 2.42 | 7.68 | 16.47 | 3.06 | 1.35 | 0.00 | 7.62 | 0.00 | 0.00 | 0.00 | 0.00 | 129470.61 | 135024 | 619397.62 | 1084626 | 178.16 | 374.93 | 8 | 121478168 | 32 | 20 | 20 | 0 |
