# Runtime Benchmark

- Camera path: edge midpoint to terrain center
- Duration per algorithm: 10 seconds
- Detailed CSV: `runtime-benchmark-20260715-223926.csv`

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
| Classic CPU ROAM | 1127 | 8.88 | 16.56 | 8.16 | 15.19 | 6.99 | 0.28 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 13620.84 | 18786 | 27582.75 | 39630 | 87.36 | 635.29 | 1 | 0 | 0 | 14 | 14 | 0 |
| Data-Oriented CPU ROAM | 1695 | 5.91 | 86.90 | 5.43 | 9.03 | 3.91 | 0.28 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 13855.91 | 18786 | 28065.19 | 39630 | 321.82 | 3936.39 | 8 | 0 | 0 | 14 | 14 | 0 |
| GPU ROAM-like | 1594 | 6.29 | 22.40 | 5.97 | 11.48 | 2.80 | 0.45 | 0.32 | 3.71 | 0.83 | 0.12 | 0.11 | 0.00 | 0.23 | 13749.54 | 18786 | 27835.66 | 39630 | 244.56 | 3680.48 | 8 | 4438608 | 24 | 14 | 14 | 0 |
