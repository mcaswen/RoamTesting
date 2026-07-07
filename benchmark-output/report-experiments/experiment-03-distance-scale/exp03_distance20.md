# Runtime Benchmark

- Camera path: edge midpoint to terrain center
- Duration per algorithm: 10 seconds
- Detailed CSV: `runtime-benchmark-20260707-141450.csv`

- Build configuration: RelWithDebInfo
- Benchmark label: exp03_distance20
- GPU device: NVIDIA GeForce RTX 5090 D/PCIe/SSE2 (4.3.0 NVIDIA 591.86)
- GPU timing model: CPU DOD topology baseline plus GPU split-only, compaction, mesh emit and indirect draw; GPU ms measures compute passes only
- VSync: disabled during benchmark

- Height map: `assets/heightmaps/Hm_Terrain_Test_129.pgm` 129x129
- Terrain size: 59.9
- Height scale: 12
- Max depth setting: 20
- Distance scale: 20
- Split/Merge thresholds: 0.04 / 0.02

| Algorithm | Samples | Avg Frame ms | Max Frame ms | Avg LOD ms | Max LOD ms | Avg CPU Update ms | Avg CPU Upload ms | Avg GPU ms | Max GPU ms | Avg GPU Snapshot ms | Avg GPU Alloc ms | Avg GPU Dispatch Wall ms | Avg GPU Query Wait ms | Avg GPU Readback Wait ms | Avg Triangles | Max Triangles | Avg Nodes | Max Nodes | Avg CPU % | Max CPU % | Max Workers | Max Upload B | Max Readback B | Config Max Depth | Reached Max Depth | Max Topology Issues |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| Classic CPU ROAM | 2332 | 4.29 | 7.63 | 3.88 | 6.12 | 3.32 | 0.13 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 7336.09 | 9563 | 14670.59 | 19128 | 88.56 | 1242.99 | 1 | 0 | 0 | 20 | 15 | 0 |
| Data-Oriented CPU ROAM | 4396 | 2.28 | 86.41 | 2.04 | 4.18 | 1.44 | 0.12 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 7293.17 | 9563 | 14584.73 | 19128 | 342.43 | 10829.80 | 8 | 0 | 0 | 20 | 15 | 0 |
| GPU ROAM-like | 3165 | 3.16 | 11.46 | 3.04 | 6.33 | 1.19 | 0.21 | 0.28 | 1.68 | 0.39 | 0.00 | 0.14 | 0.00 | 0.39 | 7409.32 | 9563 | 14817.09 | 19128 | 238.24 | 7491.76 | 8 | 2142384 | 24 | 20 | 15 | 0 |
