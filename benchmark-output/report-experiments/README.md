# 运行时基准测试实验索引

所有原始运行时基准报告均按实验目录整理。每个目录包含原始 Markdown、原始 CSV、生成的 `summary.csv`、生成的 `summary.md`，以及用于撰写报告的 SVG 图表。

> 历史数据说明（2026-07-29）：这些报告生成于 Classic、Data-Oriented 和 GPU ROAM-like 采用当前完整方差、像素 SSE、视锥感知和硬预算契约之前。它们仍是旧实现的有效记录，但耗时和三角形数量不能与当前 `HEAD` 直接比较。特别是 `experiment-03-distance-scale` 描述的是已废弃的距离加权启发式方法；`DistanceScale` 已从当前 ROAM 设置中移除。

| Experiment | Raw CSV count | Main generated files |
| --- | ---: | --- |
| `experiment-01-baseline` | 3 | summary.md, summary.csv, chart_avg_cpu_percent.svg, chart_avg_frame_ms.svg, chart_avg_lod_ms.svg, chart_avg_triangles.svg, chart_gpu_breakdown_ms.svg |
| `experiment-02-max-depth` | 5 | summary.md, summary.csv, chart_avg_cpu_percent.svg, chart_avg_frame_ms.svg, chart_avg_lod_ms.svg, chart_avg_triangles.svg, chart_gpu_breakdown_ms.svg |
| `experiment-03-distance-scale` | 4 | summary.md, summary.csv, chart_avg_cpu_percent.svg, chart_avg_frame_ms.svg, chart_avg_lod_ms.svg, chart_avg_triangles.svg, chart_gpu_breakdown_ms.svg |
| `experiment-04-heightmap` | 2 | summary.md, summary.csv, chart_avg_cpu_percent.svg, chart_avg_frame_ms.svg, chart_avg_lod_ms.svg, chart_avg_triangles.svg, chart_gpu_breakdown_ms.svg |
| `experiment-05-gpu-breakdown` | 1 | summary.md, summary.csv, chart_avg_cpu_percent.svg, chart_avg_frame_ms.svg, chart_avg_lod_ms.svg, chart_avg_triangles.svg, chart_gpu_breakdown_ms.svg |

## 基线快速汇总

| Algorithm | Avg LOD ms mean | Avg frame ms mean | Avg triangles mean | Avg CPU % mean |
| --- | ---: | ---: | ---: | ---: |
| Classic CPU ROAM | 24.65 | 26.40 | 31235 | 96.75 |
| Data-Oriented CPU ROAM | 10.49 | 11.41 | 31422 | 324.04 |
| GPU ROAM-like | 13.38 | 13.67 | 31450 | 230.30 |
