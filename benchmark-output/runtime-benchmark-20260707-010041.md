# Runtime Benchmark

- Camera path: edge midpoint to terrain center
- Duration per algorithm: 10 seconds
- Detailed CSV: `runtime-benchmark-20260707-010041.csv`

- Build configuration: RelWithDebInfo
- GPU device: NVIDIA GeForce RTX 5090 D/PCIe/SSE2 (4.3.0 NVIDIA 591.86)
- GPU timing model: CPU DOD topology baseline plus GPU split-only, compaction, mesh emit and indirect draw; GPU ms measures compute passes only
- VSync: disabled during benchmark

- Height map: `assets/heightmaps/Hm_Terrain_Test_129.pgm` 129x129
- Terrain size: 30
- Height scale: 4
- Max depth setting: 14
- Distance scale: 24
- Split/Merge thresholds: 0.04 / 0.02

| Algorithm | Samples | Avg Frame ms | Max Frame ms | Avg LOD ms | Max LOD ms | Avg CPU Update ms | Avg CPU Upload ms | Avg GPU ms | Max GPU ms | Avg Triangles | Max Triangles | Avg Nodes | Max Nodes | Avg CPU % | Max CPU % | Max Workers | Max Upload B | Max Readback B | Config Max Depth | Reached Max Depth | Max Topology Issues |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| Classic CPU ROAM | 1970 | 5.08 | 10.48 | 4.61 | 9.42 | 3.88 | 0.11 | 0.00 | 0.00 | 6477.96 | 8901 | 13068.18 | 18396 | 100.31 | 137.35 | 1 | 0 | 0 | 14 | 14 | 0 |
| Data-Oriented CPU ROAM | 3338 | 3.00 | 109.76 | 2.60 | 4.85 | 1.82 | 0.10 | 0.00 | 0.00 | 6599.34 | 8901 | 13321.03 | 18396 | 100.08 | 153.89 | 8 | 0 | 0 | 14 | 14 | 0 |
| GPU ROAM-like | 2422 | 4.14 | 15.22 | 3.79 | 7.91 | 1.35 | 0.33 | 0.08 | 2.84 | 6680.27 | 8901 | 13491.63 | 18396 | 99.83 | 141.92 | 8 | 2126964 | 88 | 14 | 14 | 0 |
