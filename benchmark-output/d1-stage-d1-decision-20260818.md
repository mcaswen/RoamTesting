# D1/O6 增量 Mesh 基础阶段报告

> 日期：2026-08-18
> 结论：DOD 增量 Mesh 基础契约完成；自适应增量/全量选择未实现。

## 实现边界

- DOD 与 Classic 共用稳定稠密 slot、split 继承、merge move-last、topology edit replay、dirty range 和 borrowed Mesh 语义；
- DOD 使用独立 `NodeSlots<uint32_t>` SoA 反向索引和 `SlotOwners<NodeIndex>`，并行 topology worker 不直接写 Mesh；
- edit 在并行 join 后的共享活动索引更新点由主线程记录，拓扑稳定后统一重放；
- 本阶段始终只生成 dirty slots，不按 dirty 比例切换到完整 emit；首次初始化和 topology-only 后重新进入 CPU 路径除外；
- 该能力是补齐 Classic 已有逻辑，不作为 DOD 独有优化。

## 默认路径

同机 OpenGL runtime 单轮对比：

| DOD 阶段 | C2 基线 | D1 | 变化 |
| --- | ---: | ---: | ---: |
| CPU update | 1.6710 ms | 0.9924 ms | -40.6% |
| Mesh emit | 0.6923 ms | 0.0498 ms | -92.8% |
| CPU upload | 0.2048 ms | 0.0227 ms | -88.9% |

D3D12 runtime 中 DOD `CPU update=1.0693 ms`、`Mesh emit=0.0484 ms`、`CPU upload=0.0123 ms`，增量统计为一次初始化、平均更新 `164.96` 个三角形、复用 `10534.70` 个三角形。

## 稳定帧回归

`incremental-emit` profile：

| Frame | Full rebuild | Updated | Reused | Dirty ranges | Mesh emit |
| --- | ---: | ---: | ---: | ---: | ---: |
| 初始化 | 1 | 528 | 0 | 1 | 0.5646 ms |
| debug 过渡 | 0 | 528 | 0 | 1 | 0.0677 ms |
| 稳定复用 | 0 | 0 | 528 | 0 | 0.0003 ms |

## 压力边界

预算饱和场景排除初始化样本后的中位数：

| 指标 | C2 全量基线 | D1 固定增量 |
| --- | ---: | ---: |
| CPU update | 198.906 ms | 243.643 ms |
| Mesh emit | 17.1440 ms | 19.5109 ms |
| Updated triangles | 200000 | 138923 |
| Reused triangles | 0 | 61076 |
| Dirty ranges | 1/full | 36738 |

固定增量在约 69% 槽位变化且 ranges 高度碎片化时慢于完整并行输出。该结果记录为后续交叉点/选择策略的输入；按本阶段决定不加入自动切换。

## 验证

- OpenGL 与 D3D12 `RelWithDebInfo` 构建通过；
- CTest 12/12 通过，包含新增 `roam_incremental_mesh_emit_dod`；
- OpenGL、D3D12 `--gpu-smoke-test` 通过；
- DOD smoke、incremental-emit、budget-saturation 均通过，拓扑错误为 0；
- validator 覆盖 slot owner、node-to-slot、局部索引、UV、range 边界和 `updated + reused == active`。

原始数据：

- `benchmark-output/d1-incremental-emit-final-20260818.csv`
- `benchmark-output/d1-smoke-20260818.csv`
- `benchmark-output/d1-budget-saturation-sorted-20260818.csv`
- `benchmark-output/runtime-benchmark-20260818-010513.md/.csv`（OpenGL）
- `benchmark-output/runtime-benchmark-20260818-010532.md/.csv`（D3D12）
