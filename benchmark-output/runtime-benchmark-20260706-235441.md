# Runtime Benchmark

- Camera path: edge midpoint to terrain center
- Duration per algorithm: 10 seconds
- Detailed CSV: `runtime-benchmark-20260706-235441.csv`

- Height map: `assets/heightmaps/Hm_Terrain_Test_129.pgm` 129x129
- Terrain size: 30
- Height scale: 4
- Max depth setting: 20
- Distance scale: 80
- Split/Merge thresholds: 0.04 / 0.02

| Algorithm | Samples | Avg Frame ms | Max Frame ms | Avg ROAM ms | Max ROAM ms | Avg GPU ms | Max GPU ms | Avg Triangles | Max Triangles | Avg Nodes | Max Nodes | Avg CPU % | Max CPU % | Max Workers | Max Upload B | Max Readback B | Config Max Depth | Reached Max Depth | Max Topology Issues |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| Classic CPU ROAM | 543 | 18.45 | 31.68 | 14.01 | 25.36 | 0.00 | 0.00 | 24443.42 | 31318 | 48898.95 | 62844 | 100.22 | 108.01 | 1 | 0 | 0 | 20 | 17 | 0 |
| Data-Oriented CPU ROAM | 1211 | 8.30 | 39.54 | 5.05 | 9.53 | 0.00 | 0.00 | 24592.25 | 31318 | 49197.61 | 62844 | 100.05 | 117.13 | 8 | 0 | 0 | 20 | 17 | 0 |
