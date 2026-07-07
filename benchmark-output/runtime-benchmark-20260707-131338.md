# Runtime Benchmark

- Camera path: edge midpoint to terrain center
- Duration per algorithm: 10 seconds
- Detailed CSV: `runtime-benchmark-20260707-131338.csv`

- Build configuration: RelWithDebInfo
- GPU device: NVIDIA GeForce RTX 5090 D/PCIe/SSE2 (4.3.0 NVIDIA 591.86)
- GPU timing model: CPU DOD topology baseline plus GPU split-only, compaction, mesh emit and indirect draw; GPU ms measures compute passes only
- VSync: disabled during benchmark

- Height map: `assets/heightmaps/Hm_Terrain_Test_129.pgm` 129x129
- Terrain size: 59.9
- Height scale: 12
- Max depth setting: 20
- Distance scale: 80
- Split/Merge thresholds: 0.04 / 0.02

| Algorithm | Samples | Avg Frame ms | Max Frame ms | Avg LOD ms | Max LOD ms | Avg CPU Update ms | Avg CPU Upload ms | Avg GPU ms | Max GPU ms | Avg GPU Snapshot ms | Avg GPU Alloc ms | Avg GPU Dispatch Wall ms | Avg GPU Query Wait ms | Avg GPU Readback Wait ms | Avg Triangles | Max Triangles | Avg Nodes | Max Nodes | Avg CPU % | Max CPU % | Max Workers | Max Upload B | Max Readback B | Config Max Depth | Reached Max Depth | Max Topology Issues |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| Classic CPU ROAM | 450 | 22.27 | 31.78 | 21.76 | 31.24 | 17.19 | 1.07 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 31320.10 | 37136 | 62638.20 | 74270 | 100.34 | 198.12 | 1 | 0 | 0 | 20 | 17 | 0 |
| Data-Oriented CPU ROAM | 1101 | 9.13 | 42.28 | 8.80 | 15.75 | 6.38 | 0.25 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 31496.12 | 37136 | 62990.23 | 74270 | 334.59 | 2010.97 | 8 | 0 | 0 | 20 | 17 | 0 |
| GPU ROAM-like | 927 | 10.83 | 29.21 | 10.42 | 15.97 | 4.35 | 1.02 | 0.47 | 1.78 | 1.51 | 0.21 | 0.11 | 0.00 | 0.95 | 31429.96 | 37136 | 62857.91 | 74270 | 206.27 | 1737.80 | 8 | 8318288 | 24 | 20 | 17 | 0 |
