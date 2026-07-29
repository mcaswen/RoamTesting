# Runtime Benchmark Experiment Index

All raw runtime benchmark reports are organized per experiment folder. Each folder includes raw Markdown, raw CSV, generated `summary.csv`, generated `summary.md`, and SVG figures for report writing.

> Historical-data notice (2026-07-29): these reports were generated before Classic CPU ROAM adopted complete variance trees, pixel-space SSE, frustum-aware LOD, a 20,000 active-triangle budget, and cascading merge. They remain valid records of the old implementation, but their Classic timings/triangle counts are not directly comparable with current `HEAD`. In particular, `experiment-03-distance-scale` describes the retired Classic distance-weighted heuristic; `DistanceScale` now applies only to the DOD/GPU ROAM-like paths.

| Experiment | Raw CSV count | Main generated files |
| --- | ---: | --- |
| `experiment-01-baseline` | 3 | summary.md, summary.csv, chart_avg_cpu_percent.svg, chart_avg_frame_ms.svg, chart_avg_lod_ms.svg, chart_avg_triangles.svg, chart_gpu_breakdown_ms.svg |
| `experiment-02-max-depth` | 5 | summary.md, summary.csv, chart_avg_cpu_percent.svg, chart_avg_frame_ms.svg, chart_avg_lod_ms.svg, chart_avg_triangles.svg, chart_gpu_breakdown_ms.svg |
| `experiment-03-distance-scale` | 4 | summary.md, summary.csv, chart_avg_cpu_percent.svg, chart_avg_frame_ms.svg, chart_avg_lod_ms.svg, chart_avg_triangles.svg, chart_gpu_breakdown_ms.svg |
| `experiment-04-heightmap` | 2 | summary.md, summary.csv, chart_avg_cpu_percent.svg, chart_avg_frame_ms.svg, chart_avg_lod_ms.svg, chart_avg_triangles.svg, chart_gpu_breakdown_ms.svg |
| `experiment-05-gpu-breakdown` | 1 | summary.md, summary.csv, chart_avg_cpu_percent.svg, chart_avg_frame_ms.svg, chart_avg_lod_ms.svg, chart_avg_triangles.svg, chart_gpu_breakdown_ms.svg |

## Quick baseline aggregate

| Algorithm | Avg LOD ms mean | Avg frame ms mean | Avg triangles mean | Avg CPU % mean |
| --- | ---: | ---: | ---: | ---: |
| Classic CPU ROAM | 24.65 | 26.40 | 31235 | 96.75 |
| Data-Oriented CPU ROAM | 10.49 | 11.41 | 31422 | 324.04 |
| GPU ROAM-like | 13.38 | 13.67 | 31450 | 230.30 |
