# Runtime Benchmark

- Camera path: edge midpoint to terrain center
- Duration per algorithm: 10 seconds
- Detailed CSV: `runtime-benchmark-20260707-141538.csv`

- Build configuration: RelWithDebInfo
- Benchmark label: exp03_distance40
- GPU device: NVIDIA GeForce RTX 5090 D/PCIe/SSE2 (4.3.0 NVIDIA 591.86)
- GPU timing model: CPU DOD topology baseline plus GPU split-only, compaction, mesh emit and indirect draw; GPU ms measures compute passes only
- VSync: disabled during benchmark

- Height map: `assets/heightmaps/Hm_Terrain_Test_129.pgm` 129x129
- Terrain size: 59.9
- Height scale: 12
- Max depth setting: 20
- Distance scale: 40
- Split/Merge thresholds: 0.04 / 0.02

| Algorithm | Samples | Avg Frame ms | Max Frame ms | Avg LOD ms | Max LOD ms | Avg CPU Update ms | Avg CPU Upload ms | Avg GPU ms | Max GPU ms | Avg GPU Snapshot ms | Avg GPU Alloc ms | Avg GPU Dispatch Wall ms | Avg GPU Query Wait ms | Avg GPU Readback Wait ms | Avg Triangles | Max Triangles | Avg Nodes | Max Nodes | Avg CPU % | Max CPU % | Max Workers | Max Upload B | Max Readback B | Config Max Depth | Reached Max Depth | Max Topology Issues |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| Classic CPU ROAM | 918 | 10.90 | 17.01 | 9.91 | 15.90 | 8.29 | 0.34 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 16282.74 | 20175 | 32563.47 | 40348 | 95.50 | 422.37 | 1 | 0 | 0 | 20 | 15 | 0 |
| Data-Oriented CPU ROAM | 1819 | 5.51 | 106.45 | 5.00 | 9.15 | 3.55 | 0.33 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 16316.37 | 20175 | 32630.75 | 40348 | 322.00 | 3845.68 | 8 | 0 | 0 | 20 | 15 | 0 |
| GPU ROAM-like | 1526 | 6.57 | 20.01 | 6.35 | 12.99 | 2.41 | 0.43 | 0.44 | 1.70 | 0.87 | 0.00 | 0.18 | 0.00 | 0.97 | 16300.85 | 20175 | 32599.70 | 40348 | 227.07 | 2885.57 | 8 | 4519024 | 24 | 20 | 15 | 0 |
