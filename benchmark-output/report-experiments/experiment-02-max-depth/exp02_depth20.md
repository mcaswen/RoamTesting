# Runtime Benchmark

- Camera path: edge midpoint to terrain center
- Duration per algorithm: 10 seconds
- Detailed CSV: `runtime-benchmark-20260707-141402.csv`

- Build configuration: RelWithDebInfo
- Benchmark label: exp02_depth20
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
| Classic CPU ROAM | 377 | 26.60 | 46.25 | 24.87 | 43.63 | 19.86 | 0.65 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 31375.89 | 37136 | 62749.79 | 74270 | 95.15 | 250.66 | 1 | 0 | 0 | 20 | 17 | 0 |
| Data-Oriented CPU ROAM | 938 | 10.73 | 86.64 | 9.77 | 18.70 | 6.61 | 0.64 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 31441.04 | 37136 | 62880.07 | 74270 | 321.28 | 2098.97 | 8 | 0 | 0 | 20 | 17 | 0 |
| GPU ROAM-like | 736 | 13.65 | 33.44 | 13.32 | 25.68 | 5.24 | 0.84 | 0.66 | 3.21 | 1.70 | 0.00 | 0.26 | 0.00 | 1.98 | 31374.72 | 37136 | 62747.44 | 74270 | 209.83 | 1406.83 | 8 | 8318288 | 24 | 20 | 17 | 0 |
