# Runtime Benchmark

- Camera path: edge midpoint to terrain center
- Duration per algorithm: 10 seconds
- Detailed CSV: `runtime-benchmark-20260707-141936.csv`

- Build configuration: RelWithDebInfo
- Benchmark label: exp05_gpu_breakdown
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
| Classic CPU ROAM | 385 | 26.05 | 42.30 | 24.34 | 40.46 | 19.50 | 0.65 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 31221.42 | 37136 | 62440.84 | 74270 | 97.90 | 251.59 | 1 | 0 | 0 | 20 | 17 | 0 |
| Data-Oriented CPU ROAM | 908 | 11.07 | 87.59 | 10.12 | 18.57 | 6.76 | 0.64 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 31446.37 | 37136 | 62890.74 | 74270 | 330.83 | 1762.50 | 8 | 0 | 0 | 20 | 17 | 0 |
| GPU ROAM-like | 730 | 13.76 | 34.38 | 13.43 | 20.03 | 5.34 | 0.86 | 0.68 | 3.84 | 1.68 | 0.00 | 0.26 | 0.00 | 1.99 | 31301.19 | 37136 | 62600.38 | 74270 | 220.93 | 1550.27 | 8 | 8318288 | 24 | 20 | 17 | 0 |
