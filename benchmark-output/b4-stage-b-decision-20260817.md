# B4 阶段 B 决策报告

## 1. 结论

B4 不包含新的算法功能，其职责是验证 B1–B3 是否已经消除 DOD 串行 topology 的实现性额外开销，并决定是否进入阶段 C。

本轮结论是：**进入阶段 C**。

DOD 的串行 split/merge 核心已经不慢于 Classic。压力路径中仍可观察到的总 topology 差距，主要来自并行 split 的 chunk 构建，以及 merge 后的活动索引和持久队列批量刷新。这些成本正是阶段 C 的 O3/O8 所覆盖的问题，不需要再增加新的优化项。

## 2. 输入与证据

- 构建：OpenGL、RelWithDebInfo
- CPU 采样：Windows Performance Recorder 的 CPU profile
- 符号：本次构建生成的 `ParallelROAM.pdb`
- 默认路径：[runtime-benchmark-20260817-193203.md](runtime-benchmark-20260817-193203.md)
- 默认路径逐帧数据：[runtime-benchmark-20260817-193203.csv](runtime-benchmark-20260817-193203.csv)
- 压力路径逐帧数据：[b4-profile-run.csv](b4-profile-run.csv)
- 压力场景：Peking 513 HeightMap、Terrain size 80、Height scale 12、Max depth 20、split/merge thresholds 0.25/0.10 px、活动叶三角形预算 200000
- WPR 目标进程：`ParallelROAM.exe`，PID 41812
- 有效 CPU 样本：18396
- 落在项目模块中的自身采样：13526，占 73.53%

原始 `b4-cpu-profile.etl` 约 704 MB，仅保留在本地并由 `.gitignore` 排除，不进入仓库。

## 3. 计时口径

所有单次操作成本均使用：

```text
单次 split 成本 = sum(cpuSplitTopology) / sum(splitCount)
单次 merge 成本 = sum(cpuMergeTopology) / sum(mergeCount)
```

聚合时排除每种算法 `timeSeconds == 0` 的初始化样本。压力路径的 headless profile 启用了 DOD 并行 split，因此同时报告总 topology 和计时器单独记录的串行收敛区间。Classic 没有并行预提交，其 topology total 就是串行参照。

## 4. 默认路径

| 算法 | 有效帧 | split 次数 | split 单次成本 | merge 次数 | merge 单次成本 | 平均 CPU update |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| Classic CPU ROAM | 599 | 12073 | 1.920 us | 16121 | 1.034 us | 2.1318 ms |
| Data-Oriented CPU ROAM | 599 | 12073 | 2.561 us | 16121 | 1.576 us | 1.7671 ms |

默认路径中的相对差距看起来较大，但 topology 总量很小：DOD 的 split/merge topology 相对 Classic 合计只多约 0.0185 ms/帧。DOD 的候选评分和整体 CPU update 已经更快，因此不能仅凭小分母下的百分比继续改写串行拓扑核心。

## 5. 压力路径工作量

| 算法 | 有效帧 | 活动叶三角形范围 | split 次数 | merge 次数 |
| --- | ---: | ---: | ---: | ---: |
| Classic CPU ROAM | 23 | 200000–200000 | 878879 | 878879 |
| Data-Oriented CPU ROAM | 23 | 199999–200000 | 879591 | 879591 |

DOD 的操作数只比 Classic 多约 0.08%，因此后续耗时差异不是通过减少工作量得到的。

## 6. 压力路径 topology 分解

### Split

| 区间 | Classic | DOD |
| --- | ---: | ---: |
| Topology total | 80.041 ms | 94.236 ms |
| 候选排序/chunk 构建 | 0 | 15.384 ms |
| 队列邻域失效 | 0 | 0.095 ms |
| 并行提交 | 0 | 0.080 ms |
| worker 结果汇总 | 0 | 0.002 ms |
| 活动索引/队列刷新 | 0 | 0.225 ms |
| 串行收敛 | 80.041 ms | 78.324 ms |

DOD 总 split topology 比 Classic 多 14.195 ms，但其串行收敛反而少 1.717 ms。总差距几乎完全由并行路径在本轮候选规模下构建 chunk 造成。`SplitNodeImpl` 本身不是剩余瓶颈。

### Merge

| 区间 | Classic | DOD |
| --- | ---: | ---: |
| Topology total | 47.265 ms | 51.020 ms |
| 候选排序/chunk 构建 | 0 | 0.384 ms |
| 队列邻域失效 | 0 | 2.115 ms |
| 并行提交 | 0 | 0.781 ms |
| worker 结果汇总 | 0 | 0.017 ms |
| 活动索引/队列刷新 | 0 | 8.800 ms |
| 串行收敛 | 未单独填充 | 38.818 ms |

Classic 没有填充 DOD 专用的 merge 子阶段字段，因此用 47.265 ms 的 topology total 作为串行参照。DOD 串行收敛比该参照少 8.447 ms，但批量索引和队列维护增加约 12.097 ms，最终总计多 3.755 ms。

## 7. WPR 函数采样

下表是函数包含样本，包含其子调用。两种算法的拓扑操作数近似相同，因此可以用于定位相对热点，但不把样本数直接换算为绝对毫秒。

| 职责 | Classic | 样本 | DOD | 样本 |
| --- | --- | ---: | --- | ---: |
| split 核心 | `ClassicRoamMeshBuilder::SplitNode` | 2126 | `SplitNodeImpl<SerialTopologyCommitPolicy>` | 2031 |
| merge 核心 | `ClassicRoamMeshBuilder::MergeNodeOrDiamond` | 1112 | `MergeNodeOrDiamondWithScoreLimitImpl<SerialTopologyCommitPolicy>` | 897 |
| split heap 比较 | `SplitEntryPrecedes` | 148 | `SplitEntryPrecedes` | 308 |
| merge 邻域刷新 | `RefreshMergeQueueNeighborhood` | 626 | `RefreshPersistentMergeQueueNeighborhood` | 818 |
| merge 入队检查 | `InsertMergeQueueNodeIfEligible` | 610 | `InsertMergeQueueNodeIfEligible` | 829 |
| merge 候选移除 | `RemoveMergeQueueCandidate` | 204 | `RemovePersistentMergeQueueCandidate` | 185 |

源码位置：

- Classic split：[ClassicRoamTopology.cpp](../src/algorithms/classic_roam/ClassicRoamTopology.cpp) 中的 `ClassicRoamMeshBuilder::SplitNode`
- Classic merge：[ClassicRoamTopology.cpp](../src/algorithms/classic_roam/ClassicRoamTopology.cpp) 中的 `ClassicRoamMeshBuilder::MergeNodeOrDiamond`
- DOD split/merge：[DataOrientedRoamTopology.cpp](../src/algorithms/data_oriented_roam/DataOrientedRoamTopology.cpp) 中的 `SplitNodeImpl` 和 `MergeNodeOrDiamondWithScoreLimitImpl`
- DOD split heap：[DataOrientedRoamQueues.cpp](../src/algorithms/data_oriented_roam/DataOrientedRoamQueues.cpp) 中的 `SplitEntryPrecedes`
- DOD merge queue：[DataOrientedRoamQueues.cpp](../src/algorithms/data_oriented_roam/DataOrientedRoamQueues.cpp) 中的 `RefreshPersistentMergeQueueNeighborhood` 和 `InsertMergeQueueNodeIfEligible`

## 8. B4 决策

1. 不再继续重写 DOD 的串行 `SplitNodeImpl` 或 `MergeSingleNodeImpl`
2. 进入 C1/O3，分离活动叶连续视图与 split heap，减少 heap 比较时从 node index 跳转读取 score/path 的成本
3. 随后执行 C2/O8，重组活动状态和 merge queue 旁路元数据，降低批量刷新时的分散数组访问
4. 并行 split 的 chunk 构建成本单独保留为候选规模策略问题，不把它误记为串行 topology 较慢
5. 后续 A/B 继续同时报告 topology total、串行收敛和真实 split/merge 操作数
