# Runtime Benchmark

- Camera path: edge midpoint to terrain center
- Sampled duration per algorithm: 0.2 seconds
- Detailed CSV: `runtime-benchmark-20260716-005731.csv`

- Build configuration: Debug
- Graphics backend: D3D12
- Graphics adapter: NVIDIA GeForce RTX 5090 D (Direct3D 12 (feature level 12_0); driver 32.0.15.9186)
- Benchmark label: dx12-gpu-peking
- GPU timing model: DX12 compute topology refinement, GPU mesh emission and ExecuteIndirect
- VSync: disabled during benchmark

- Height map: `assets/heightmaps/Hm_Terrain_Peking_513.png` 547x547
- Terrain size: 30
- Height scale: 4
- Max depth setting: 14
- Distance scale: 24
- Split/Merge thresholds: 0.04 / 0.02

| Algorithm | Samples | Avg Frame ms | Max Frame ms | Avg LOD ms | Max LOD ms | Avg CPU Update ms | Avg CPU Upload ms | Avg GPU ms | Max GPU ms | Avg GPU Snapshot ms | Avg GPU Alloc ms | Avg GPU Dispatch Wall ms | Avg GPU Query Wait ms | Avg GPU Readback Wait ms | Avg Frame Fence Wait ms | Max Frame Fence Wait ms | Avg Render ms | Max Render ms | Avg Triangles | Max Triangles | Avg Nodes | Max Nodes | Avg CPU % | Max CPU % | Max Workers | Max Upload B | Max Readback B | Config Max Depth | Reached Max Depth | Max Topology Issues |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| Classic CPU ROAM | 7 | 33.85 | 58.19 | 37.33 | 57.12 | 31.95 | 2.12 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.01 | 0.01 | 7132.29 | 10392 | 15292.57 | 24202 | 112.56 | 149.56 | 1 | 0 | 0 | 14 | 14 | 0 |
| Data-Oriented CPU ROAM | 10 | 28.09 | 72.72 | 22.40 | 31.12 | 18.44 | 0.36 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.01 | 0.01 | 7165.80 | 10321 | 15198.40 | 24328 | 306.90 | 662.47 | 8 | 0 | 0 | 14 | 14 | 0 |
| GPU ROAM-like | 8 | 31.97 | 36.45 | 30.37 | 35.59 | 18.14 | 0.00 | 0.02 | 0.02 | 1.27 | 0.00 | 0.21 | 0.00 | 0.00 | 0.00 | 0.00 | 0.04 | 0.05 | 5869.88 | 10176 | 16507.75 | 24258 | 157.66 | 329.35 | 8 | 2716920 | 40 | 14 | 14 | 0 |
