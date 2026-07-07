# Experiment 05 Gpu Breakdown

Generated from the raw runtime benchmark CSV files in this folder.

## Per-case summary

| Case | Algorithm | Samples | Avg LOD ms | Avg Frame ms | Avg Triangles | Avg CPU % | Avg GPU ms | Max Depth | Topology Issues |
| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| baseline | Classic CPU ROAM | 385 | 24.34 | 26.05 | 31221 | 97.90 | 0.00 | 17 | 0 |
| baseline | Data-Oriented CPU ROAM | 908 | 10.12 | 11.07 | 31446 | 330.83 | 0.00 | 17 | 0 |
| baseline | GPU ROAM-like | 730 | 13.43 | 13.76 | 31301 | 220.93 | 0.68 | 17 | 0 |

## Figures

- `chart_avg_cpu_percent.svg`
- `chart_avg_frame_ms.svg`
- `chart_avg_lod_ms.svg`
- `chart_avg_triangles.svg`
- `chart_gpu_breakdown_ms.svg`
