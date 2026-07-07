# Experiment 02 Max Depth

Generated from the raw runtime benchmark CSV files in this folder.

## Per-case summary

| Case | Algorithm | Samples | Avg LOD ms | Avg Frame ms | Avg Triangles | Avg CPU % | Avg GPU ms | Max Depth | Topology Issues |
| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| depth 12 | Classic CPU ROAM | 2503 | 3.65 | 4.00 | 8116 | 77.15 | 0.00 | 12 | 0 |
| depth 12 | Data-Oriented CPU ROAM | 3958 | 2.24 | 2.53 | 8117 | 308.21 | 0.00 | 12 | 0 |
| depth 12 | GPU ROAM-like | 3096 | 3.11 | 3.23 | 8116 | 222.89 | 0.30 | 12 | 0 |
| depth 14 | Classic CPU ROAM | 547 | 16.88 | 18.32 | 26100 | 99.35 | 0.00 | 14 | 0 |
| depth 14 | Data-Oriented CPU ROAM | 1210 | 7.54 | 8.29 | 26102 | 322.12 | 0.00 | 14 | 0 |
| depth 14 | GPU ROAM-like | 965 | 10.09 | 10.40 | 26113 | 212.42 | 0.62 | 14 | 0 |
| depth 16 | Classic CPU ROAM | 388 | 24.13 | 25.85 | 31156 | 96.39 | 0.00 | 16 | 0 |
| depth 16 | Data-Oriented CPU ROAM | 934 | 9.80 | 10.77 | 31459 | 328.45 | 0.00 | 16 | 0 |
| depth 16 | GPU ROAM-like | 741 | 13.22 | 13.55 | 31296 | 196.68 | 0.66 | 16 | 0 |
| depth 18 | Classic CPU ROAM | 370 | 25.27 | 27.05 | 31315 | 99.37 | 0.00 | 17 | 0 |
| depth 18 | Data-Oriented CPU ROAM | 925 | 9.90 | 10.88 | 31447 | 310.23 | 0.00 | 17 | 0 |
| depth 18 | GPU ROAM-like | 721 | 13.59 | 13.93 | 31378 | 223.66 | 0.68 | 17 | 0 |
| depth 20 | Classic CPU ROAM | 377 | 24.87 | 26.60 | 31376 | 95.15 | 0.00 | 17 | 0 |
| depth 20 | Data-Oriented CPU ROAM | 938 | 9.77 | 10.73 | 31441 | 321.28 | 0.00 | 17 | 0 |
| depth 20 | GPU ROAM-like | 736 | 13.32 | 13.65 | 31375 | 209.83 | 0.66 | 17 | 0 |

## Figures

- `chart_avg_cpu_percent.svg`
- `chart_avg_frame_ms.svg`
- `chart_avg_lod_ms.svg`
- `chart_avg_triangles.svg`
- `chart_gpu_breakdown_ms.svg`
