# Runtime Benchmark

- Camera path: edge midpoint to terrain center
- Sampled duration per algorithm: 0.2 seconds
- Detailed CSV: `runtime-benchmark-20260716-005115.csv`

- Build configuration: RelWithDebInfo
- Graphics backend: D3D12
- Graphics adapter: NVIDIA GeForce RTX 5090 D (Direct3D 12 (feature level 12_0); driver 32.0.15.9186)
- Benchmark label: dx12-gpu-like
- GPU timing model: DX12 compute topology refinement, GPU mesh emission and ExecuteIndirect
- VSync: disabled during benchmark

- Height map: `assets/heightmaps/Hm_Terrain_Test_129.pgm` 129x129
- Terrain size: 30
- Height scale: 4
- Max depth setting: 14
- Distance scale: 24
- Split/Merge thresholds: 0.04 / 0.02

| Algorithm | Samples | Avg Frame ms | Max Frame ms | Avg LOD ms | Max LOD ms | Avg CPU Update ms | Avg CPU Upload ms | Avg GPU ms | Max GPU ms | Avg GPU Snapshot ms | Avg GPU Alloc ms | Avg GPU Dispatch Wall ms | Avg GPU Query Wait ms | Avg GPU Readback Wait ms | Avg Frame Fence Wait ms | Max Frame Fence Wait ms | Avg Render ms | Max Render ms | Avg Triangles | Max Triangles | Avg Nodes | Max Nodes | Avg CPU % | Max CPU % | Max Workers | Max Upload B | Max Readback B | Config Max Depth | Reached Max Depth | Max Topology Issues |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| Classic CPU ROAM | 17 | 12.05 | 18.26 | 11.57 | 17.62 | 7.73 | 2.41 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.01 | 0.01 | 13700.29 | 18799 | 27783.65 | 39614 | 114.89 | 252.44 | 1 | 0 | 0 | 14 | 14 | 0 |
| Data-Oriented CPU ROAM | 44 | 5.28 | 25.08 | 4.51 | 7.67 | 3.35 | 0.17 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.01 | 0.01 | 13971.14 | 18784 | 28349.82 | 39630 | 233.71 | 2613.64 | 8 | 0 | 0 | 14 | 14 | 0 |
| GPU ROAM-like | 22 | 9.52 | 14.80 | 9.17 | 13.69 | 2.84 | 0.00 | 0.02 | 0.03 | 0.75 | 0.00 | 0.21 | 0.00 | 0.00 | 0.00 | 0.00 | 0.04 | 0.05 | 12931.36 | 18703 | 28574.45 | 39630 | 36.84 | 173.43 | 8 | 4438584 | 40 | 14 | 14 | 0 |
