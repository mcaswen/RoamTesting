# Runtime Benchmark

- Camera path: edge midpoint to terrain center
- Duration per algorithm: 10 seconds
- Detailed CSV: `runtime-benchmark-20260715-230725.csv`

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

| Algorithm | Samples | Avg Frame ms | Max Frame ms | Avg LOD ms | Max LOD ms | Avg CPU Update ms | Avg CPU Upload ms | Avg GPU ms | Max GPU ms | Avg GPU Snapshot ms | Avg GPU Alloc ms | Avg GPU Dispatch Wall ms | Avg GPU Query Wait ms | Avg GPU Readback Wait ms | Avg Triangles | Max Triangles | Avg Nodes | Max Nodes | Avg CPU % | Max CPU % | Max Workers | Max Upload B | Max Readback B | Config Max Depth | Reached Max Depth | Max Topology Issues |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| Classic CPU ROAM | 1096 | 9.13 | 25.42 | 8.37 | 24.23 | 7.16 | 0.30 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 13611.42 | 18786 | 27568.14 | 39630 | 91.10 | 615.70 | 1 | 0 | 0 | 14 | 14 | 0 |
| Data-Oriented CPU ROAM | 1702 | 5.89 | 146.69 | 5.40 | 11.64 | 4.00 | 0.29 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 13926.00 | 18786 | 28203.28 | 39630 | 357.37 | 4045.44 | 8 | 0 | 0 | 14 | 14 | 0 |
| GPU ROAM-like | 1391 | 7.21 | 20.74 | 7.01 | 12.91 | 2.80 | 0.39 | 0.41 | 3.72 | 0.85 | 0.00 | 0.18 | 0.00 | 0.96 | 13891.38 | 18786 | 28139.11 | 39630 | 230.31 | 3142.60 | 8 | 4438608 | 24 | 14 | 14 | 0 |
