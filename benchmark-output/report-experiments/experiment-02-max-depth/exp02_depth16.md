# Runtime Benchmark

- Camera path: edge midpoint to terrain center
- Duration per algorithm: 10 seconds
- Detailed CSV: `runtime-benchmark-20260707-141223.csv`

- Build configuration: RelWithDebInfo
- Benchmark label: exp02_depth16
- GPU device: NVIDIA GeForce RTX 5090 D/PCIe/SSE2 (4.3.0 NVIDIA 591.86)
- GPU timing model: CPU DOD topology baseline plus GPU split-only, compaction, mesh emit and indirect draw; GPU ms measures compute passes only
- VSync: disabled during benchmark

- Height map: `assets/heightmaps/Hm_Terrain_Test_129.pgm` 129x129
- Terrain size: 59.9
- Height scale: 12
- Max depth setting: 16
- Distance scale: 80
- Split/Merge thresholds: 0.04 / 0.02

| Algorithm | Samples | Avg Frame ms | Max Frame ms | Avg LOD ms | Max LOD ms | Avg CPU Update ms | Avg CPU Upload ms | Avg GPU ms | Max GPU ms | Avg GPU Snapshot ms | Avg GPU Alloc ms | Avg GPU Dispatch Wall ms | Avg GPU Query Wait ms | Avg GPU Readback Wait ms | Avg Triangles | Max Triangles | Avg Nodes | Max Nodes | Avg CPU % | Max CPU % | Max Workers | Max Upload B | Max Readback B | Config Max Depth | Reached Max Depth | Max Topology Issues |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| Classic CPU ROAM | 388 | 25.85 | 41.64 | 24.13 | 39.17 | 19.34 | 0.64 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 31156.50 | 36960 | 62311.00 | 73918 | 96.39 | 287.33 | 1 | 0 | 0 | 16 | 16 | 0 |
| Data-Oriented CPU ROAM | 934 | 10.77 | 89.37 | 9.80 | 19.24 | 6.68 | 0.65 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 31459.35 | 36960 | 62916.71 | 73918 | 328.45 | 1937.80 | 8 | 0 | 0 | 16 | 16 | 0 |
| GPU ROAM-like | 741 | 13.55 | 34.10 | 13.22 | 24.11 | 5.22 | 0.82 | 0.66 | 2.15 | 1.69 | 0.00 | 0.24 | 0.00 | 1.96 | 31296.06 | 36960 | 62590.12 | 73918 | 196.68 | 1467.74 | 8 | 8278864 | 24 | 16 | 16 | 0 |
