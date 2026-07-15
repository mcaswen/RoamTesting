# Runtime Benchmark

- Camera path: edge midpoint to terrain center
- Sampled duration per algorithm: 0.2 seconds
- Detailed CSV: `runtime-benchmark-20260716-002230.csv`

- Build configuration: RelWithDebInfo
- Graphics backend: D3D12
- Graphics adapter: NVIDIA GeForce RTX 5090 D (Direct3D 12 (feature level 12_0); driver 32.0.15.9186)
- Benchmark label: dx12-stage5-peking-smoke
- GPU ROAM-like skipped: D3D12 GPU topology is intentionally deferred to migration stage 6
- VSync: disabled during benchmark

- Height map: `assets/heightmaps/Hm_Terrain_Peking_513.png` 547x547
- Terrain size: 30
- Height scale: 4
- Max depth setting: 14
- Distance scale: 24
- Split/Merge thresholds: 0.04 / 0.02

| Algorithm | Samples | Avg Frame ms | Max Frame ms | Avg LOD ms | Max LOD ms | Avg CPU Update ms | Avg CPU Upload ms | Avg GPU ms | Max GPU ms | Avg GPU Snapshot ms | Avg GPU Alloc ms | Avg GPU Dispatch Wall ms | Avg GPU Query Wait ms | Avg GPU Readback Wait ms | Avg Render ms | Max Render ms | Avg Triangles | Max Triangles | Avg Nodes | Max Nodes | Avg CPU % | Max CPU % | Max Workers | Max Upload B | Max Readback B | Config Max Depth | Reached Max Depth | Max Topology Issues |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| Classic CPU ROAM | 27 | 7.52 | 15.05 | 6.91 | 10.80 | 4.40 | 1.82 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.01 | 0.01 | 7423.67 | 10322 | 15670.96 | 24364 | 75.79 | 410.20 | 1 | 0 | 0 | 14 | 14 | 0 |
| Data-Oriented CPU ROAM | 76 | 2.88 | 13.37 | 2.49 | 5.73 | 1.82 | 0.12 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.01 | 0.01 | 7290.50 | 10322 | 15371.11 | 24368 | 242.07 | 4886.82 | 8 | 0 | 0 | 14 | 14 | 0 |
