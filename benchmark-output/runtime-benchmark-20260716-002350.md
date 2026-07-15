# Runtime Benchmark

- Camera path: edge midpoint to terrain center
- Sampled duration per algorithm: 0.1 seconds
- Detailed CSV: `runtime-benchmark-20260716-002350.csv`

- Build configuration: RelWithDebInfo
- Graphics backend: OpenGL
- Graphics adapter: NVIDIA GeForce RTX 5090 D/PCIe/SSE2 (4.3.0 NVIDIA 591.86)
- Benchmark label: opengl-stage5-regression
- GPU device: NVIDIA GeForce RTX 5090 D/PCIe/SSE2 (4.3.0 NVIDIA 591.86)
- GPU timing model: CPU DOD topology baseline plus GPU split-only, compaction, mesh emit and indirect draw; GPU ms measures compute passes only
- VSync: disabled during benchmark

- Height map: `assets/heightmaps/Hm_Terrain_Test_129.pgm` 129x129
- Terrain size: 30
- Height scale: 4
- Max depth setting: 14
- Distance scale: 24
- Split/Merge thresholds: 0.04 / 0.02

| Algorithm | Samples | Avg Frame ms | Max Frame ms | Avg LOD ms | Max LOD ms | Avg CPU Update ms | Avg CPU Upload ms | Avg GPU ms | Max GPU ms | Avg GPU Snapshot ms | Avg GPU Alloc ms | Avg GPU Dispatch Wall ms | Avg GPU Query Wait ms | Avg GPU Readback Wait ms | Avg Render ms | Max Render ms | Avg Triangles | Max Triangles | Avg Nodes | Max Nodes | Avg CPU % | Max CPU % | Max Workers | Max Upload B | Max Readback B | Config Max Depth | Reached Max Depth | Max Topology Issues |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| Classic CPU ROAM | 10 | 10.69 | 16.81 | 10.74 | 16.13 | 8.55 | 0.30 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 13291.30 | 18812 | 27004.40 | 39566 | 119.45 | 391.56 | 1 | 0 | 0 | 14 | 14 | 0 |
| Data-Oriented CPU ROAM | 3 | 51.71 | 125.26 | 8.25 | 13.84 | 6.64 | 0.23 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 9900.00 | 17325 | 20246.67 | 35994 | 186.83 | 444.00 | 8 | 0 | 0 | 14 | 14 | 0 |
| GPU ROAM-like | 14 | 9.30 | 25.47 | 7.70 | 10.27 | 3.31 | 0.39 | 0.23 | 0.65 | 0.74 | 0.00 | 0.25 | 0.00 | 0.97 | 0.00 | 0.00 | 13731.57 | 18801 | 27894.29 | 39602 | 232.98 | 1485.80 | 8 | 4435472 | 24 | 14 | 14 | 0 |
