# Experiment 04 Heightmap

Generated from the raw runtime benchmark CSV files in this folder.

## Per-case summary

| Case | Algorithm | Samples | Avg LOD ms | Avg Frame ms | Avg Triangles | Avg CPU % | Avg GPU ms | Max Depth | Topology Issues |
| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| Peking513 | Classic CPU ROAM | 1503 | 6.17 | 6.66 | 10799 | 88.55 | 0.00 | 18 | 0 |
| Peking513 | Data-Oriented CPU ROAM | 2690 | 3.35 | 3.72 | 10824 | 300.93 | 0.00 | 18 | 0 |
| Peking513 | GPU ROAM-like | 2262 | 4.27 | 4.43 | 10835 | 233.32 | 0.35 | 18 | 0 |
| Test129 | Classic CPU ROAM | 363 | 25.85 | 27.61 | 31335 | 95.61 | 0.00 | 17 | 0 |
| Test129 | Data-Oriented CPU ROAM | 940 | 9.75 | 10.70 | 31461 | 329.71 | 0.00 | 17 | 0 |
| Test129 | GPU ROAM-like | 741 | 13.22 | 13.55 | 31384 | 222.98 | 0.68 | 17 | 0 |

## Figures

- `chart_avg_cpu_percent.svg`
- `chart_avg_frame_ms.svg`
- `chart_avg_lod_ms.svg`
- `chart_avg_triangles.svg`
- `chart_gpu_breakdown_ms.svg`
