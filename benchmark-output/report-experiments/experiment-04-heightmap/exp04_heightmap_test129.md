# Runtime Benchmark

- Camera path: edge midpoint to terrain center
- Duration per algorithm: 10 seconds
- Detailed CSV: `runtime-benchmark-20260707-141759.csv`

- Build configuration: RelWithDebInfo
- Benchmark label: exp04_heightmap_test129
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
| Classic CPU ROAM | 363 | 27.61 | 44.35 | 25.85 | 42.44 | 20.64 | 0.66 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 31334.77 | 37136 | 62667.54 | 74270 | 95.61 | 246.48 | 1 | 0 | 0 | 20 | 17 | 0 |
| Data-Oriented CPU ROAM | 940 | 10.70 | 86.52 | 9.75 | 19.16 | 6.56 | 0.65 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 31460.52 | 37136 | 62919.04 | 74270 | 329.71 | 1951.05 | 8 | 0 | 0 | 20 | 17 | 0 |
| GPU ROAM-like | 741 | 13.55 | 33.63 | 13.22 | 20.61 | 5.20 | 0.86 | 0.68 | 2.17 | 1.70 | 0.00 | 0.26 | 0.00 | 1.99 | 31384.48 | 37136 | 62766.96 | 74270 | 222.98 | 1404.49 | 8 | 8318288 | 24 | 20 | 17 | 0 |
