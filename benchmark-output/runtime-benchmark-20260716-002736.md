# 运行时基准测试报告

- 相机路径：从地形边缘中点移动到地形中心
- 每种算法的采样时长：0.1 秒
- 详细 CSV：`runtime-benchmark-20260716-002736.csv`

- 构建配置：RelWithDebInfo
- 图形后端：D3D12
- 图形适配器：NVIDIA GeForce RTX 5090 D (Direct3D 12 (feature level 12_0); driver 32.0.15.9186)
- Benchmark 标签：dx12-final-regression
- 跳过 GPU ROAM-like：D3D12 GPU 拓扑按计划延后到迁移阶段 6
- VSync：基准测试期间已关闭

- Height map：`assets/heightmaps/Hm_Terrain_Test_129.pgm` 129x129
- Terrain size：30
- Height scale：4
- Max depth 设置：14
- Distance scale：24
- Split/Merge 阈值：0.04 / 0.02

| Algorithm | Samples | Avg Frame ms | Max Frame ms | Avg LOD ms | Max LOD ms | Avg CPU Update ms | Avg CPU Upload ms | Avg GPU ms | Max GPU ms | Avg GPU Snapshot ms | Avg GPU Alloc ms | Avg GPU Dispatch Wall ms | Avg GPU Query Wait ms | Avg GPU Readback Wait ms | Avg Frame Fence Wait ms | Max Frame Fence Wait ms | Avg Render ms | Max Render ms | Avg Triangles | Max Triangles | Avg Nodes | Max Nodes | Avg CPU % | Max CPU % | Max Workers | Max Upload B | Max Readback B | Config Max Depth | Reached Max Depth | Max Topology Issues |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| Classic CPU ROAM | 9 | 12.66 | 19.40 | 12.12 | 18.90 | 8.31 | 2.30 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.01 | 0.01 | 13814.00 | 18808 | 28174.00 | 39570 | 78.18 | 188.65 | 1 | 0 | 0 | 14 | 14 | 0 |
| Data-Oriented CPU ROAM | 21 | 6.10 | 21.01 | 5.03 | 8.94 | 3.76 | 0.26 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.01 | 0.01 | 13901.10 | 18799 | 28237.81 | 39626 | 456.23 | 2964.26 | 8 | 0 | 0 | 14 | 14 | 0 |
