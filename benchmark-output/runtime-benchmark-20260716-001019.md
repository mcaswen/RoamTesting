# Runtime Benchmark

- Camera path: edge midpoint to terrain center
- Duration per algorithm: 10 seconds
- Detailed CSV: `runtime-benchmark-20260716-001019.csv`

- Build configuration: RelWithDebInfo
- Graphics backend: D3D12
- Graphics adapter: NVIDIA GeForce RTX 5090 D (Direct3D 12 (feature level 12_0))
- Benchmark label: dx12-stage5-smoke
- GPU ROAM-like skipped: D3D12 GPU topology is intentionally deferred to migration stage 6
- VSync: disabled during benchmark

- Height map: `assets/heightmaps/Hm_Terrain_Test_129.pgm` 129x129
- Terrain size: 30
- Height scale: 4
- Max depth setting: 14
- Distance scale: 24
- Split/Merge thresholds: 0.04 / 0.02

| Algorithm | Samples | Avg Frame ms | Max Frame ms | Avg LOD ms | Max LOD ms | Avg CPU Update ms | Avg CPU Upload ms | Avg GPU ms | Max GPU ms | Avg GPU Snapshot ms | Avg GPU Alloc ms | Avg GPU Dispatch Wall ms | Avg GPU Query Wait ms | Avg GPU Readback Wait ms | Avg Render ms | Max Render ms | Avg Triangles | Max Triangles | Avg Nodes | Max Nodes | Avg CPU % | Max CPU % | Max Workers | Max Upload B | Max Readback B | Config Max Depth | Reached Max Depth | Max Topology Issues |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| Classic CPU ROAM | 88 | 11.43 | 18.30 | 10.91 | 17.68 | 7.30 | 2.18 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.01 | 0.01 | 13617.32 | 18784 | 27564.18 | 39630 | 86.97 | 447.00 | 1 | 0 | 0 | 14 | 14 | 0 |
| Data-Oriented CPU ROAM | 221 | 4.64 | 24.39 | 4.22 | 10.71 | 2.96 | 0.14 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.01 | 0.01 | 13412.49 | 18786 | 27113.28 | 39630 | 341.58 | 5450.90 | 8 | 0 | 0 | 14 | 14 | 0 |
