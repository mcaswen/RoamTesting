# Runtime Benchmark

- Camera path: edge midpoint to terrain center
- Duration per algorithm: 10 seconds
- Detailed CSV: `runtime-benchmark-20260707-140914.csv`

- Build configuration: RelWithDebInfo
- Benchmark label: exp01_baseline_r02
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
| Classic CPU ROAM | 378 | 26.50 | 46.20 | 24.79 | 43.89 | 19.80 | 0.64 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 31118.11 | 37136 | 62234.21 | 74270 | 97.33 | 275.41 | 1 | 0 | 0 | 20 | 17 | 0 |
| Data-Oriented CPU ROAM | 912 | 11.03 | 88.48 | 10.07 | 19.39 | 6.78 | 0.65 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 31439.36 | 37136 | 62876.73 | 74270 | 307.04 | 1844.75 | 8 | 0 | 0 | 20 | 17 | 0 |
| GPU ROAM-like | 735 | 13.66 | 33.25 | 13.33 | 22.70 | 5.34 | 0.83 | 0.68 | 3.42 | 1.70 | 0.00 | 0.25 | 0.00 | 1.94 | 31395.94 | 37136 | 62789.88 | 74270 | 214.44 | 1259.00 | 8 | 8318288 | 24 | 20 | 17 | 0 |
