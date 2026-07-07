# Runtime Benchmark

- Camera path: edge midpoint to terrain center
- Duration per algorithm: 10 seconds
- Detailed CSV: `runtime-benchmark-20260707-141136.csv`

- Build configuration: RelWithDebInfo
- Benchmark label: exp02_depth14
- GPU device: NVIDIA GeForce RTX 5090 D/PCIe/SSE2 (4.3.0 NVIDIA 591.86)
- GPU timing model: CPU DOD topology baseline plus GPU split-only, compaction, mesh emit and indirect draw; GPU ms measures compute passes only
- VSync: disabled during benchmark

- Height map: `assets/heightmaps/Hm_Terrain_Test_129.pgm` 129x129
- Terrain size: 59.9
- Height scale: 12
- Max depth setting: 14
- Distance scale: 80
- Split/Merge thresholds: 0.04 / 0.02

| Algorithm | Samples | Avg Frame ms | Max Frame ms | Avg LOD ms | Max LOD ms | Avg CPU Update ms | Avg CPU Upload ms | Avg GPU ms | Max GPU ms | Avg GPU Snapshot ms | Avg GPU Alloc ms | Avg GPU Dispatch Wall ms | Avg GPU Query Wait ms | Avg GPU Readback Wait ms | Avg Triangles | Max Triangles | Avg Nodes | Max Nodes | Avg CPU % | Max CPU % | Max Workers | Max Upload B | Max Readback B | Config Max Depth | Reached Max Depth | Max Topology Issues |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| Classic CPU ROAM | 547 | 18.32 | 29.85 | 16.88 | 28.40 | 13.38 | 0.54 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 26100.29 | 28690 | 52198.57 | 57378 | 99.35 | 278.99 | 1 | 0 | 0 | 14 | 14 | 0 |
| Data-Oriented CPU ROAM | 1210 | 8.29 | 87.41 | 7.54 | 16.86 | 5.21 | 0.53 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 26102.11 | 28690 | 52202.22 | 57378 | 322.12 | 2322.13 | 8 | 0 | 0 | 14 | 14 | 0 |
| GPU ROAM-like | 965 | 10.40 | 25.83 | 10.09 | 18.01 | 3.91 | 0.69 | 0.62 | 1.93 | 1.34 | 0.00 | 0.24 | 0.00 | 1.52 | 26112.72 | 28690 | 52223.44 | 57378 | 212.42 | 1806.96 | 8 | 6426384 | 24 | 14 | 14 | 0 |
