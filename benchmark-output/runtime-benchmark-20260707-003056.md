# Runtime Benchmark

- Camera path: edge midpoint to terrain center
- Duration per algorithm: 10 seconds
- Detailed CSV: `runtime-benchmark-20260707-003056.csv`

- GPU device: NVIDIA GeForce RTX 5090 D/PCIe/SSE2 (4.3.0 NVIDIA 591.86)

- Height map: `assets/heightmaps/Hm_Terrain_Test_129.pgm` 129x129
- Terrain size: 30
- Height scale: 4
- Max depth setting: 14
- Distance scale: 24
- Split/Merge thresholds: 0.04 / 0.02

| Algorithm | Samples | Avg Frame ms | Max Frame ms | Avg ROAM ms | Max ROAM ms | Avg GPU ms | Max GPU ms | Avg Triangles | Max Triangles | Avg Nodes | Max Nodes | Avg CPU % | Max CPU % | Max Workers | Max Upload B | Max Readback B | Config Max Depth | Reached Max Depth | Max Topology Issues |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| Classic CPU ROAM | 2109 | 4.74 | 9.69 | 3.66 | 7.94 | 0.00 | 0.00 | 6508.82 | 8901 | 13133.78 | 18396 | 100.20 | 136.84 | 1 | 0 | 0 | 14 | 14 | 0 |
| Data-Oriented CPU ROAM | 3422 | 2.93 | 160.54 | 1.78 | 3.85 | 0.00 | 0.00 | 6618.26 | 8901 | 13360.20 | 18396 | 100.13 | 148.54 | 8 | 0 | 0 | 14 | 14 | 0 |
| GPU ROAM-like | 2453 | 4.08 | 15.47 | 1.29 | 4.21 | 0.10 | 1.85 | 6695.23 | 8901 | 13523.37 | 18396 | 100.24 | 140.13 | 8 | 2126964 | 88 | 14 | 14 | 0 |
