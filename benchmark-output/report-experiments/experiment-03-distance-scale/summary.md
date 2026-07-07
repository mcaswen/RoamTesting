# Experiment 03 Distance Scale

Generated from the raw runtime benchmark CSV files in this folder.

## Per-case summary

| Case | Algorithm | Samples | Avg LOD ms | Avg Frame ms | Avg Triangles | Avg CPU % | Avg GPU ms | Max Depth | Topology Issues |
| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| distance 20 | Classic CPU ROAM | 2332 | 3.88 | 4.29 | 7336 | 88.56 | 0.00 | 15 | 0 |
| distance 20 | Data-Oriented CPU ROAM | 4396 | 2.05 | 2.28 | 7293 | 342.43 | 0.00 | 15 | 0 |
| distance 20 | GPU ROAM-like | 3165 | 3.04 | 3.16 | 7409 | 238.24 | 0.28 | 15 | 0 |
| distance 40 | Classic CPU ROAM | 918 | 9.91 | 10.90 | 16283 | 95.50 | 0.00 | 15 | 0 |
| distance 40 | Data-Oriented CPU ROAM | 1819 | 5.00 | 5.51 | 16316 | 322.00 | 0.00 | 15 | 0 |
| distance 40 | GPU ROAM-like | 1526 | 6.35 | 6.57 | 16301 | 227.07 | 0.44 | 15 | 0 |
| distance 60 | Classic CPU ROAM | 501 | 18.53 | 19.97 | 24998 | 97.08 | 0.00 | 15 | 0 |
| distance 60 | Data-Oriented CPU ROAM | 1205 | 7.57 | 8.33 | 25193 | 313.19 | 0.00 | 15 | 0 |
| distance 60 | GPU ROAM-like | 965 | 10.11 | 10.40 | 25042 | 217.84 | 0.59 | 15 | 0 |
| distance 80 | Classic CPU ROAM | 371 | 25.29 | 27.00 | 31251 | 98.81 | 0.00 | 17 | 0 |
| distance 80 | Data-Oriented CPU ROAM | 912 | 10.06 | 11.03 | 31463 | 329.08 | 0.00 | 17 | 0 |
| distance 80 | GPU ROAM-like | 719 | 13.65 | 13.96 | 31352 | 198.82 | 0.65 | 17 | 0 |

## Figures

- `chart_avg_cpu_percent.svg`
- `chart_avg_frame_ms.svg`
- `chart_avg_lod_ms.svg`
- `chart_avg_triangles.svg`
- `chart_gpu_breakdown_ms.svg`
