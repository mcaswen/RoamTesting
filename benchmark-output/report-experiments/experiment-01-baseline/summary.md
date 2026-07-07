# Experiment 01 Baseline

Generated from the raw runtime benchmark CSV files in this folder.

## Per-case summary

| Case | Algorithm | Samples | Avg LOD ms | Avg Frame ms | Avg Triangles | Avg CPU % | Avg GPU ms | Max Depth | Topology Issues |
| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| r01 | Classic CPU ROAM | 382 | 24.46 | 26.23 | 31244 | 95.88 | 0.00 | 17 | 0 |
| r01 | Data-Oriented CPU ROAM | 804 | 11.66 | 12.51 | 31338 | 337.72 | 0.00 | 17 | 0 |
| r01 | GPU ROAM-like | 726 | 13.62 | 13.83 | 31629 | 259.41 | 0.48 | 17 | 0 |
| r02 | Classic CPU ROAM | 378 | 24.79 | 26.50 | 31118 | 97.33 | 0.00 | 17 | 0 |
| r02 | Data-Oriented CPU ROAM | 912 | 10.07 | 11.03 | 31439 | 307.04 | 0.00 | 17 | 0 |
| r02 | GPU ROAM-like | 735 | 13.33 | 13.66 | 31396 | 214.44 | 0.68 | 17 | 0 |
| r03 | Classic CPU ROAM | 379 | 24.69 | 26.46 | 31343 | 97.03 | 0.00 | 17 | 0 |
| r03 | Data-Oriented CPU ROAM | 941 | 9.73 | 10.69 | 31489 | 327.35 | 0.00 | 17 | 0 |
| r03 | GPU ROAM-like | 743 | 13.19 | 13.52 | 31325 | 217.05 | 0.67 | 17 | 0 |

## Three-run aggregate

| Algorithm | Avg LOD ms mean | Avg LOD ms stdev | Avg triangles mean | Avg CPU % mean |
| --- | ---: | ---: | ---: | ---: |
| Classic CPU ROAM | 24.65 | 0.14 | 31235 | 96.75 |
| Data-Oriented CPU ROAM | 10.49 | 0.84 | 31422 | 324.04 |
| GPU ROAM-like | 13.38 | 0.18 | 31450 | 230.30 |

## Figures

- `chart_avg_cpu_percent.svg`
- `chart_avg_frame_ms.svg`
- `chart_avg_lod_ms.svg`
- `chart_avg_triangles.svg`
- `chart_gpu_breakdown_ms.svg`
