# Runtime Benchmark

- Camera path: edge midpoint to terrain center
- Sampled duration per algorithm: 0.1 seconds
- Detailed CSV: `runtime-benchmark-20260716-002736.csv`

- Build configuration: RelWithDebInfo
- Graphics backend: D3D12
- Graphics adapter: NVIDIA GeForce RTX 5090 D (Direct3D 12 (feature level 12_0); driver 32.0.15.9186)
- Benchmark label: dx12-final-regression
- GPU ROAM-like skipped: D3D12 GPU topology is intentionally deferred to migration stage 6
- VSync: disabled during benchmark

- Height map: `assets/heightmaps/Hm_Terrain_Test_129.pgm` 129x129
- Terrain size: 30
- Height scale: 4
- Max depth setting: 14
- Distance scale: 24
- Split/Merge thresholds: 0.04 / 0.02

| Algorithm | Samples | Avg Frame ms | Max Frame ms | Avg LOD ms | Max LOD ms | Avg CPU Update ms | Avg CPU Upload ms | Avg GPU ms | Max GPU ms | Avg GPU Snapshot ms | Avg GPU Alloc ms | Avg GPU Dispatch Wall ms | Avg GPU Query Wait ms | Avg GPU Readback Wait ms | Avg Frame Fence Wait ms | Max Frame Fence Wait ms | Avg Render ms | Max Render ms | Avg Triangles | Max Triangles | Avg Nodes | Max Nodes | Avg CPU % | Max CPU % | Max Workers | Max Upload B | Max Readback B | Config Max Depth | Reached Max Depth | Max Topology Issues |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| Classic CPU ROAM | 9 | 12.66 | 19.40 | 12.12 | 18.90 | 8.31 | 2.30 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.01 | 0.01 | 13814.00 | 18808 | 28174.00 | 39570 | 78.18 | 188.65 | 1 | 0 | 0 | 14 | 14 | 0 |
| Data-Oriented CPU ROAM | 21 | 6.10 | 21.01 | 5.03 | 8.94 | 3.76 | 0.26 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.01 | 0.01 | 13901.10 | 18799 | 28237.81 | 39626 | 456.23 | 2964.26 | 8 | 0 | 0 | 14 | 14 | 0 |
