# 运行时基准测试报告

- 相机路径：离散采样点序列；具体路径类型见下方 Benchmark 路径
- 每种算法的路径采样点数：600
- 采样规则：每种算法按相同 sampleIndex 执行全部相机姿态，完成后再切换算法
- 采样完整性：完整
- timeSeconds：当前算法实际经过的墙钟时间，不参与路径推进
- 详细 CSV：`runtime-benchmark-20260824-022908.csv`

- 构建配置：RelWithDebInfo
- 图形后端：D3D12
- 图形适配器：NVIDIA GeForce RTX 5090 D (Direct3D 12 (feature level 12_0); requested Agility SDK 614; runtime 6.2.26100.8972 (C:\WINDOWS\SYSTEM32\D3D12Core.dll); driver 32.0.15.9186)
- CBT 2024：已纳入相同离散路径和逐阶段 GPU 采样
- Benchmark 路径：默认选项路径
- 每种算法预热帧数：8
- CBT 参数：capacity=128K，area=50.000000 px²，validation=Off，geometry=ModifiedOnly
- 路径采样点数：600；每种算法按相同 sampleIndex 执行完整路径
- VSync：基准测试期间已关闭

- Height map：`assets/heightmaps/Hm_Terrain_Test_129.pgm` 129x129
- Terrain size：30
- Height scale：4
- Max depth 设置：14
- ROAM 屏幕空间 split/merge 阈值：4 px / 2 px
- ROAM triangle budget：20000
- DOD 并行 Split：关闭

- CBT capacity：131072
- CBT triangle area：50 px²
- CBT validation：Off
- CBT geometry：ModifiedOnly

## 总体结果

| Algorithm | Samples | Avg Frame ms | Max Frame ms | Avg LOD ms | Max LOD ms | Avg Triangles | Max Triangles | Avg Nodes | Max Nodes | Avg CPU % | Max CPU % | Max Workers | Config Max Depth | Reached Max Depth | Max Topology Issues |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| Classic CPU ROAM | 600 | 1.75 | 7.50 | 1.59 | 6.93 | 10699.66 | 12629 | 37578.82 | 49326 | 101.94 | 1559.69 | 1 | 14 | 14 | 0 |
| Data-Oriented CPU ROAM | 600 | 1.22 | 2.87 | 0.94 | 1.88 | 10699.66 | 12629 | 37578.82 | 49326 | 193.36 | 14336.51 | 8 | 14 | 14 | 0 |
| CBT 2024（GPU 常驻拓扑） | 600 | 0.25 | 3.62 | 0.03 | 0.09 | 2999.89 | 3829 | 2999.89 | 3829 | 0.00 | 0.00 | 0 | 14 | 4 | 0 |

> CPU ROAM 使用边长误差阈值，CBT 使用投影三角形面积阈值；本表可比较固定输入下的绝对开销和规模趋势，但不表示三者已经质量匹配。

## CBT 2024 GPU 阶段

GPU 阶段均来自按交换链帧轮转的 timestamp query，不等待当前帧。汇总只使用计数、compute timestamp 和 terrain draw timestamp 代次一致的新鲜样本。

- 有效对齐样本：600 / 600
- dropped/错代样本：0
- 最大诊断样本年龄：2 帧
- 平均 GPU 阶段合计：0.0568 ms
- 最大 GPU 阶段合计：0.0786 ms
- 最大 BlockingSmoke 等待：0.0000 ms

| CBT GPU 阶段 | Avg ms |
| --- | ---: |
| Classification geometry | 0.0002 |
| Reset | 0.0022 |
| Classify | 0.0041 |
| Split | 0.0054 |
| Allocate | 0.0046 |
| Neighbor copy | 0.0025 |
| Bisect | 0.0024 |
| PropagateBisect | 0.0032 |
| PrepareSimplify | 0.0041 |
| Simplify | 0.0040 |
| PropagateSimplify | 0.0034 |
| ReducePre | 0.0014 |
| ReduceFirst | 0.0020 |
| ReduceSecond | 0.0011 |
| Indexation / indirect | 0.0049 |
| Render vertex evaluation | 0.0034 |
| Validation | 0.0001 |
| Terrain render | 0.0079 |

### CBT 延迟计数

| Fresh samples | Avg active dynamic | Avg remaining dynamic | Avg split candidates | Avg simplify candidates | Avg committed slots | Avg released slots | Max readback B |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 600 | 2993.8917 | 128078.1083 | 14.5600 | 302.0600 | 9.6683 | 6.8267 | 424 |

## ROAM 逻辑阶段对比

表中数值均为平均毫秒数。Classic 与 DOD 使用相同的屏幕空间误差、迟滞阈值、活动叶预算和增量 mesh 输出语义。

| ROAM 逻辑阶段 | Classic CPU | DOD CPU | 实现说明 |
| --- | ---: | ---: | --- |
| Prepare / 帧状态 | 0.0836 | 0.0832 | 准备持久拓扑和逐帧输入 |
| Merge 候选评分 | 0.4035 | 0.0969 | 刷新持久 Q_m 的优先级 |
| Merge 拓扑提交 / 向上级联 | 0.0224 | 0.0291 | 提交 diamond merge、邻接修复和级联回收 |
| Split 前 active leaf 收集 | 0.0000 | 0.0000 | 两者均直接复用持久活动叶表示 |
| 视点相关 leaf error / 视锥测试 | 0.0000 | 0.0000 | 计入持久 Q_s 优先级刷新，因此独立字段为 0 |
| Split 扫描/标记 | 0.4353 | 0.1786 | 刷新 Q_s、建堆并生成提交候选 |
| Split 拓扑 / 裂缝约束提交 | 0.0327 | 0.0377 | 按同一预算与 forced-split 约束提交 |
| 细分后 active leaf 收集 / 输出视图 | 0.0000 | 0.0000 | Classic 复用 dense slot owners，DOD 复用 ActiveLeafNodes |
| Mesh emit / draw argument 生成 | 0.0225 | 0.0397 | 两者均只重写 dirty slot；DOD 可将较大批次分段给 worker |
| Finalize / 发布 packet | 0.5357 | 0.4596 | 汇总统计、更新跨帧状态并发布 renderer packet |

## CPU 实现阶段

`CPU update` 包含下表中互斥的物理执行区间；`CPU upload` 是算法返回后的 renderer 上传。Classic 与 DOD 都持久维护 Q_s/Q_m，并在每个 Build 刷新现有成员的优先级。DOD 对两队列的评分并行化，`Split scan/mark` 包含 Q_s 优先级刷新、原地建堆和提交快照生成，`Merge mark` 对应 Q_m 的同类工作。DOD 满预算时持续执行 merge-first 资源交换，直到 max(Q_s) 不再高于 min(Q_m)。两者单独的 `Error eval` 都为零；Classic 与 DOD 的 `Mesh emit` 都是 dirty-slot 增量更新，DOD 对较大的 dirty 批次沿用 worker 分段，因此两者差异不能解释为增量与全量策略差异。

| Algorithm | CPU update | Prepare | Merge mark | Merge topology | Budget leaf collect | Error eval | Split scan/mark | Split topology | Final leaf collect/view | Mesh emit | Finalize | CPU upload |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| Classic CPU ROAM | 1.5359 | 0.0836 | 0.4035 | 0.0224 | 0.0000 | 0.0000 | 0.4353 | 0.0327 | 0.0000 | 0.0225 | 0.5357 | 0.0478 |
| Data-Oriented CPU ROAM | 0.9250 | 0.0832 | 0.0969 | 0.0291 | 0.0000 | 0.0000 | 0.1786 | 0.0377 | 0.0000 | 0.0397 | 0.4596 | 0.0108 |

### CPU Split 拓扑阶段计时（统一口径）

六段均为互斥执行区间。Classic 不执行并行预提交，因此前五项为 0；它与 DOD 的`串行收敛` 都从候选刷新结束后开始，并扣除循环中执行的 merge。`六段合计` 与`Topology total` 的差值是函数调用、worker 数决策和少量循环控制等尚未单列的开销。

| 操作 | 候选排序/分桶 | 队列邻域失效 | chunk 并行提交 | worker 结果汇总 | 活动叶索引/队列刷新 | 串行收敛 | 六段合计 | Topology total |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| Classic Split | 0 | 0 | 0 | 0 | 0 | 0.0327 | 0.0327 | 0.0327 |
| DoD Split | 0.0000 | 0.0000 | 0.0000 | 0.0000 | 0.0000 | 0.0376 | 0.0376 | 0.0377 |
| Merge | 0.0016 | 0.0000 | 0.0000 | 0.0000 | 0.0000 | 0.0263 | 0.0280 | 0.0291 |

### 增量 CPU Mesh 输出

`Full rebuilds` 是采样窗口内的全量初始化次数；其余列是逐帧平均值。Classic 与 DOD 都填充稳定 slot、dirty range 和复用统计。D3D12 的 frame slot 会延迟消费两次使用之间积累的 dirty range 并集，因此 `Max upload bytes` 表示实际补齐量，不要求等于当前 Build 的 updated triangles。

| Algorithm | Full rebuilds | Updated triangles | Reused triangles | Dirty ranges | Max upload bytes |
| --- | ---: | ---: | ---: | ---: | ---: |
| Classic CPU ROAM | 0 | 123.0733 | 10576.5867 | 84.0483 | 2121672 |
| Data-Oriented CPU ROAM | 0 | 122.9900 | 10576.6700 | 91.4467 | 81144 |

### 原生 pass 包络

这些是各实现原有的外围 pass 计时。它们与上方互斥阶段重叠，不能重复相加。

| Algorithm | Split | Merge | Emit | Validate |
| --- | ---: | ---: | ---: | ---: |
| Classic CPU ROAM | 0.4680 | 0.4259 | 0.0225 | 0.0000 |
| Data-Oriented CPU ROAM | 0.2164 | 0.1259 | 0.0397 | 0.0000 |
| CBT 2024（GPU 常驻拓扑） | 0.0181 | 0.0115 | 0.0035 | 0.0001 |

## 渲染与上传

| Algorithm | Frame fence wait | Render | Max render | Max upload B | Max readback B |
| --- | ---: | ---: | ---: | ---: | ---: |
| Classic CPU ROAM | 0.01 | 0.01 | 0.02 | 2121672 | 0 |
| Data-Oriented CPU ROAM | 0.05 | 0.01 | 0.23 | 81144 | 0 |
| CBT 2024（GPU 常驻拓扑） | 0.05 | 0.07 | 0.09 | 0 | 424 |
