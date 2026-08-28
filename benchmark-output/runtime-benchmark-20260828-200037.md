# 运行时基准测试报告

- 相机路径：离散采样点序列；具体路径类型见下方 Benchmark 路径
- 每种算法的路径采样点数：600
- 采样规则：每种算法按相同 sampleIndex 执行全部相机姿态，完成后再切换算法
- 采样完整性：完整
- timeSeconds：当前算法实际经过的墙钟时间，不参与路径推进
- 详细 CSV：`runtime-benchmark-20260828-200037.csv`

- 构建配置：Release
- 图形后端：D3D12
- 图形适配器：NVIDIA GeForce RTX 5090 D (Direct3D 12 (feature level 12_0); requested Agility SDK 614; runtime 6.2.26100.8972 (C:\WINDOWS\SYSTEM32\D3D12Core.dll); driver 32.0.15.9186)
- Benchmark 标签：five-round-1280x720-default-r02
- 算法集合：Classic、Data-Oriented、CBT 2024
- CBT 2024：已纳入相同离散路径和逐阶段 GPU 采样
- Benchmark 路径：默认选项路径
- 每种算法预热帧数：16
- CBT 参数：capacity=128K，area=58.000000 px²，validation=Off，geometry=ModifiedOnly
- 路径采样点数：600；每种算法按相同 sampleIndex 执行完整路径
- CBT 基线标识：cbt-2024-official-baseline-v1
- VSync：基准测试期间已关闭

- Drawable 分辨率：1280x720
- Height map：`assets/heightmaps/Hm_Terrain_Test_129.pgm` 129x129
- Terrain size：30
- Height scale：4
- Max depth 设置：20
- ROAM 屏幕空间 split/merge 阈值：4 px / 2 px
- ROAM triangle budget：20000
- DOD 并行 Split：关闭

- CBT capacity：131072
- CBT triangle area：58 px²
- CBT validation：Off
- CBT geometry：ModifiedOnly

## 总体结果

| Algorithm | Samples | Avg Frame ms | Max Frame ms | Avg LOD ms | Max LOD ms | Avg Triangles | Max Triangles | Avg Nodes | Max Nodes | Avg CPU % | Max CPU % | Max Workers | Config Max Depth | Reached Max Depth | Max Topology Issues |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| Classic CPU ROAM | 600 | 4.70 | 8.07 | 4.50 | 7.83 | 18758.02 | 19608 | 117377.00 | 205068 | 99.29 | 463.86 | 1 | 20 | 19 | 0 |
| Data-Oriented CPU ROAM | 600 | 3.55 | 9.44 | 3.34 | 9.26 | 18758.02 | 19608 | 117377.00 | 205068 | 242.86 | 5011.63 | 8 | 20 | 19 | 0 |
| CBT 2024 | 600 | 0.33 | 0.90 | 0.04 | 0.16 | 18732.14 | 20028 | 18732.14 | 20028 | 0.00 | 0.00 | 0 | 20 | 20 | 0 |

> CPU ROAM 使用边长误差阈值，CBT 使用投影三角形面积阈值；本表可比较固定输入下的绝对开销和规模趋势，但不表示三者已经质量匹配。

## CBT 2024 GPU 阶段

GPU 阶段均来自按交换链帧轮转的 timestamp query，不等待当前帧。汇总只使用计数、compute timestamp 和 terrain draw timestamp 代次一致的新鲜样本。

- 有效对齐样本：600 / 600
- dropped/错代样本：0
- 最大诊断样本年龄：2 帧
- 平均 GPU 阶段合计：0.0798 ms
- 最大 GPU 阶段合计：0.2898 ms
- 最大 BlockingSmoke 等待：0.0000 ms

| CBT GPU 阶段 | Avg ms |
| --- | ---: |
| Classification geometry | 0.0003 |
| Reset | 0.0022 |
| Classify | 0.0041 |
| Split | 0.0076 |
| Allocate | 0.0076 |
| Neighbor copy | 0.0025 |
| Bisect | 0.0043 |
| PropagateBisect | 0.0044 |
| PrepareSimplify | 0.0046 |
| Simplify | 0.0055 |
| PropagateSimplify | 0.0046 |
| ReducePre | 0.0014 |
| ReduceFirst | 0.0022 |
| ReduceSecond | 0.0011 |
| Indexation / indirect | 0.0049 |
| Render vertex evaluation | 0.0050 |
| Validation | 0.0015 |
| Terrain render | 0.0159 |

### CBT 延迟计数

| Fresh samples | Avg active dynamic | Avg remaining dynamic | Avg split candidates | Avg simplify candidates | Avg committed slots | Avg released slots | Max readback B |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 600 | 18726.1417 | 112345.8583 | 118.8850 | 1292.5733 | 149.8083 | 148.0983 | 428 |

## ROAM 逻辑阶段对比

表中数值均为平均毫秒数。Classic 与 DOD 使用相同的屏幕空间误差、迟滞阈值、活动叶预算和增量 mesh 输出语义。

| ROAM 逻辑阶段 | Classic CPU | DOD CPU | 实现说明 |
| --- | ---: | ---: | --- |
| Prepare / 帧状态 | 0.1598 | 0.2355 | 准备持久拓扑和逐帧输入 |
| Merge 候选评分 | 0.6088 | 0.2603 | 刷新持久 Q_m 的优先级 |
| Merge 拓扑提交 / 向上级联 | 0.0930 | 0.1670 | 提交 diamond merge、邻接修复和级联回收 |
| Split 前 active leaf 收集 | 0.0000 | 0.0000 | 两者均直接复用持久活动叶表示 |
| 视点相关 leaf error / 视锥测试 | 0.0000 | 0.0000 | 计入持久 Q_s 优先级刷新，因此独立字段为 0 |
| Split 扫描/标记 | 1.9200 | 0.6658 | 刷新 Q_s、建堆并生成提交候选 |
| Split 拓扑 / 裂缝约束提交 | 0.2086 | 0.3177 | 按同一预算与 forced-split 约束提交 |
| 细分后 active leaf 收集 / 输出视图 | 0.0000 | 0.0000 | Classic 复用 dense slot owners，DOD 复用 ActiveLeafNodes |
| Mesh emit / draw argument 生成 | 0.1903 | 0.2421 | 两者均只重写 dirty slot；DOD 可将较大批次分段给 worker |
| Finalize / 发布 packet | 1.1500 | 1.3093 | 汇总统计、更新跨帧状态并发布 renderer packet |

## CPU 实现阶段

`CPU update` 包含下表中互斥的物理执行区间；`CPU upload` 是算法返回后的 renderer 上传。Classic 与 DOD 都持久维护 Q_s/Q_m，并在每个 Build 刷新现有成员的优先级。DOD 对两队列的评分并行化，`Split scan/mark` 包含 Q_s 优先级刷新、原地建堆和提交快照生成，`Merge mark` 对应 Q_m 的同类工作。DOD 满预算时持续执行 merge-first 资源交换，直到 max(Q_s) 不再高于 min(Q_m)。两者单独的 `Error eval` 都为零；Classic 与 DOD 的 `Mesh emit` 都是 dirty-slot 增量更新，DOD 对较大的 dirty 批次沿用 worker 分段，因此两者差异不能解释为增量与全量策略差异。

| Algorithm | CPU update | Prepare | Merge mark | Merge topology | Budget leaf collect | Error eval | Split scan/mark | Split topology | Final leaf collect/view | Mesh emit | Finalize | CPU upload |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| Classic CPU ROAM | 4.3307 | 0.1598 | 0.6088 | 0.0930 | 0.0000 | 0.0000 | 1.9200 | 0.2086 | 0.0000 | 0.1903 | 1.1500 | 0.1641 |
| Data-Oriented CPU ROAM | 3.1979 | 0.2355 | 0.2603 | 0.1670 | 0.0000 | 0.0000 | 0.6658 | 0.3177 | 0.0000 | 0.2421 | 1.3093 | 0.1347 |

### CPU Split 拓扑阶段计时（统一口径）

六段均为互斥执行区间。Classic 不执行并行预提交，因此前五项为 0；它与 DOD 的`串行收敛` 都从候选刷新结束后开始，并扣除循环中执行的 merge。`六段合计` 与`Topology total` 的差值是函数调用、worker 数决策和少量循环控制等尚未单列的开销。

| 操作 | 候选排序/分桶 | 队列邻域失效 | chunk 并行提交 | worker 结果汇总 | 活动叶索引/队列刷新 | 串行收敛 | 六段合计 | Topology total |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| Classic Split | 0 | 0 | 0 | 0 | 0 | 0.2086 | 0.2086 | 0.2086 |
| DoD Split | 0.0000 | 0.0000 | 0.0000 | 0.0000 | 0.0000 | 0.3175 | 0.3175 | 0.3177 |
| Merge | 0.0075 | 0.0000 | 0.0000 | 0.0000 | 0.0000 | 0.1570 | 0.1645 | 0.1670 |

### 增量 CPU Mesh 输出

`Full rebuilds` 是采样窗口内的全量初始化次数；其余列是逐帧平均值。Classic 与 DOD 都填充稳定 slot、dirty range 和复用统计。D3D12 的 frame slot 会延迟消费两次使用之间积累的 dirty range 并集，因此 `Max upload bytes` 表示实际补齐量，不要求等于当前 Build 的 updated triangles。

| Algorithm | Full rebuilds | Updated triangles | Reused triangles | Dirty ranges | Max upload bytes |
| --- | ---: | ---: | ---: | ---: | ---: |
| Classic CPU ROAM | 0 | 1340.3733 | 17417.6467 | 930.2383 | 3294144 |
| Data-Oriented CPU ROAM | 0 | 1340.3650 | 17417.6550 | 1039.3867 | 516936 |

### 原生 pass 包络

这些是各实现原有的外围 pass 计时。它们与上方互斥阶段重叠，不能重复相加。

| Algorithm | Split | Merge | Emit | Validate |
| --- | ---: | ---: | ---: | ---: |
| Classic CPU ROAM | 2.1285 | 0.7019 | 0.1903 | 0.0000 |
| Data-Oriented CPU ROAM | 0.9835 | 0.4273 | 0.2421 | 0.0000 |
| CBT 2024 | 0.0265 | 0.0146 | 0.0052 | 0.0015 |

## 渲染与上传

| Algorithm | Frame fence wait | Render | Max render | Max upload B | Max readback B |
| --- | ---: | ---: | ---: | ---: | ---: |
| Classic CPU ROAM | 0.00 | 0.01 | 0.21 | 3294144 | 0 |
| Data-Oriented CPU ROAM | 0.00 | 0.01 | 0.20 | 516936 | 0 |
| CBT 2024 | 0.10 | 0.09 | 0.30 | 0 | 428 |
