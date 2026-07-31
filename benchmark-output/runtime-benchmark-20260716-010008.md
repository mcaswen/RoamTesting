# 运行时基准测试报告

- 相机路径：从地形边缘中点移动到地形中心
- 每种算法的采样时长：0.1 秒
- 详细 CSV：`runtime-benchmark-20260716-010008.csv`

- 构建配置：RelWithDebInfo
- 图形后端：D3D12
- 图形适配器：NVIDIA GeForce RTX 5090 D (Direct3D 12 (feature level 12_0); driver 32.0.15.9186)
- Benchmark 标签：dx12-gpu-final
- GPU 计时模型：DX12 compute 执行拓扑细分和 GPU mesh emit，并使用 ExecuteIndirect
- VSync：基准测试期间已关闭

- Height map：`assets/heightmaps/Hm_Terrain_Test_129.pgm` 129x129
- Terrain size：30
- Height scale：4
- Max depth 设置：14
- Distance scale：24
- Split/Merge 阈值：0.04 / 0.02

| Algorithm | Samples | Avg Frame ms | Max Frame ms | Avg LOD ms | Max LOD ms | Avg CPU Update ms | Avg CPU Upload ms | Avg GPU ms | Max GPU ms | Avg GPU Snapshot ms | Avg GPU Alloc ms | Avg GPU Dispatch Wall ms | Avg GPU Query Wait ms | Avg GPU Readback Wait ms | Avg Frame Fence Wait ms | Max Frame Fence Wait ms | Avg Render ms | Max Render ms | Avg Triangles | Max Triangles | Avg Nodes | Max Nodes | Avg CPU % | Max CPU % | Max Workers | Max Upload B | Max Readback B | Config Max Depth | Reached Max Depth | Max Topology Issues |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| Classic CPU ROAM | 9 | 12.38 | 19.21 | 12.07 | 18.55 | 8.50 | 2.16 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.01 | 0.01 | 13876.44 | 18804 | 28260.22 | 39558 | 50.05 | 180.55 | 1 | 0 | 0 | 14 | 14 | 0 |
| Data-Oriented CPU ROAM | 22 | 5.72 | 22.42 | 4.64 | 7.92 | 3.43 | 0.24 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.01 | 0.01 | 13831.14 | 18799 | 28067.82 | 39626 | 170.45 | 1827.06 | 8 | 0 | 0 | 14 | 14 | 0 |
| GPU ROAM-like | 10 | 10.90 | 13.98 | 10.99 | 13.12 | 3.59 | 0.00 | 0.02 | 0.03 | 0.67 | 4.66 | 0.20 | 0.00 | 0.00 | 0.00 | 0.00 | 0.04 | 0.06 | 11348.10 | 18106 | 28065.40 | 39558 | 152.17 | 745.49 | 8 | 4430520 | 40 | 14 | 14 | 0 |
