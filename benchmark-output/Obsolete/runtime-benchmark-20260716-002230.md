# 运行时基准测试报告

- 相机路径：从地形边缘中点移动到地形中心
- 每种算法的采样时长：0.2 秒
- 详细 CSV：`runtime-benchmark-20260716-002230.csv`

- 构建配置：RelWithDebInfo
- 图形后端：D3D12
- 图形适配器：NVIDIA GeForce RTX 5090 D (Direct3D 12 (feature level 12_0); driver 32.0.15.9186)
- Benchmark 标签：dx12-stage5-peking-smoke
- 跳过 GPU ROAM-like：D3D12 GPU 拓扑按计划延后到迁移阶段 6
- VSync：基准测试期间已关闭

- Height map：`assets/heightmaps/Hm_Terrain_Peking_513.png` 547x547
- Terrain size：30
- Height scale：4
- Max depth 设置：14
- Distance scale：24
- Split/Merge 阈值：0.04 / 0.02

| Algorithm | Samples | Avg Frame ms | Max Frame ms | Avg LOD ms | Max LOD ms | Avg CPU Update ms | Avg CPU Upload ms | Avg GPU ms | Max GPU ms | Avg GPU Snapshot ms | Avg GPU Alloc ms | Avg GPU Dispatch Wall ms | Avg GPU Query Wait ms | Avg GPU Readback Wait ms | Avg Render ms | Max Render ms | Avg Triangles | Max Triangles | Avg Nodes | Max Nodes | Avg CPU % | Max CPU % | Max Workers | Max Upload B | Max Readback B | Config Max Depth | Reached Max Depth | Max Topology Issues |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| Classic CPU ROAM | 27 | 7.52 | 15.05 | 6.91 | 10.80 | 4.40 | 1.82 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.01 | 0.01 | 7423.67 | 10322 | 15670.96 | 24364 | 75.79 | 410.20 | 1 | 0 | 0 | 14 | 14 | 0 |
| Data-Oriented CPU ROAM | 76 | 2.88 | 13.37 | 2.49 | 5.73 | 1.82 | 0.12 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.01 | 0.01 | 7290.50 | 10322 | 15371.11 | 24368 | 242.07 | 4886.82 | 8 | 0 | 0 | 14 | 14 | 0 |
