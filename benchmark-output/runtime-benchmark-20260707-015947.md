# Runtime Benchmark

- Camera path: edge midpoint to terrain center
- Duration per algorithm: 10 seconds
- Detailed CSV: `runtime-benchmark-20260707-015947.csv`

- Build configuration: RelWithDebInfo
- GPU device: NVIDIA GeForce RTX 5090 D/PCIe/SSE2 (4.3.0 NVIDIA 591.86)
- GPU timing model: CPU DOD topology baseline plus GPU split-only, compaction, mesh emit and indirect draw; GPU ms measures compute passes only
- VSync: disabled during benchmark

- Height map: `assets/heightmaps/Hm_Terrain_Test_129.pgm` 129x129
- Terrain size: 61.7
- Height scale: 12
- Max depth setting: 20
- Distance scale: 60.8
- Split/Merge thresholds: 0.04 / 0.02

| Algorithm | Samples | Avg Frame ms | Max Frame ms | Avg LOD ms | Max LOD ms | Avg CPU Update ms | Avg CPU Upload ms | Avg GPU ms | Max GPU ms | Avg GPU Snapshot ms | Avg GPU Alloc ms | Avg GPU Dispatch Wall ms | Avg GPU Query Wait ms | Avg GPU Readback Wait ms | Avg Triangles | Max Triangles | Avg Nodes | Max Nodes | Avg CPU % | Max CPU % | Max Workers | Max Upload B | Max Readback B | Config Max Depth | Reached Max Depth | Max Topology Issues |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| Classic CPU ROAM | 682 | 14.66 | 27.53 | 14.25 | 27.06 | 12.10 | 0.25 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 24909.14 | 31215 | 49816.28 | 62428 | 99.99 | 108.85 | 1 | 0 | 0 | 20 | 15 | 0 |
| Data-Oriented CPU ROAM | 1394 | 7.20 | 36.77 | 6.93 | 13.01 | 5.06 | 0.19 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 24994.84 | 31215 | 49987.67 | 62428 | 100.72 | 118.72 | 8 | 0 | 0 | 20 | 15 | 0 |
| GPU ROAM-like | 1000 | 10.04 | 28.37 | 9.66 | 17.18 | 3.45 | 0.77 | 0.44 | 1.63 | 2.58 | 0.19 | 0.11 | 0.00 | 0.81 | 24979.83 | 31215 | 49957.67 | 62428 | 99.85 | 111.34 | 8 | 6991984 | 24 | 20 | 15 | 0 |
