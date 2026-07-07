# Runtime Benchmark

- Camera path: edge midpoint to terrain center
- Duration per algorithm: 10 seconds
- Detailed CSV: `runtime-benchmark-20260707-141002.csv`

- Build configuration: RelWithDebInfo
- Benchmark label: exp01_baseline_r03
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
| Classic CPU ROAM | 379 | 26.46 | 42.16 | 24.69 | 40.04 | 19.70 | 0.65 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 31343.15 | 37136 | 62684.30 | 74270 | 97.03 | 249.76 | 1 | 0 | 0 | 20 | 17 | 0 |
| Data-Oriented CPU ROAM | 941 | 10.69 | 87.49 | 9.73 | 18.50 | 6.60 | 0.64 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 31488.77 | 37136 | 62975.54 | 74270 | 327.35 | 2012.30 | 8 | 0 | 0 | 20 | 17 | 0 |
| GPU ROAM-like | 743 | 13.52 | 33.01 | 13.19 | 21.43 | 5.18 | 0.83 | 0.67 | 2.13 | 1.67 | 0.00 | 0.26 | 0.00 | 1.96 | 31324.61 | 37136 | 62647.22 | 74270 | 217.05 | 1388.64 | 8 | 8318288 | 24 | 20 | 17 | 0 |
