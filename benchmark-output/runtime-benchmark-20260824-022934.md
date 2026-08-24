# 运行时基准测试报告

- 相机路径：离散采样点序列；具体路径类型见下方 Benchmark 路径
- 每种算法的路径采样点数：64
- 采样规则：每种算法按相同 sampleIndex 执行全部相机姿态，完成后再切换算法
- 采样完整性：完整
- timeSeconds：当前算法实际经过的墙钟时间，不参与路径推进
- 详细 CSV：`runtime-benchmark-20260824-022934.csv`

- 构建配置：RelWithDebInfo
- 图形后端：D3D12
- 图形适配器：NVIDIA GeForce RTX 5090 D (Direct3D 12 (feature level 12_0); requested Agility SDK 614; runtime 6.2.26100.8972 (C:\WINDOWS\SYSTEM32\D3D12Core.dll); driver 32.0.15.9186)
- CBT 2024：已纳入相同离散路径和逐阶段 GPU 采样
- Benchmark 路径：极限压力路径
- 每种算法预热帧数：8
- CBT 参数：capacity=1M，area=25.000000 px²，validation=Off，geometry=ModifiedOnly
- 路径采样点数：64；每种算法按相同 sampleIndex 执行完整路径
- VSync：基准测试期间已关闭

- Height map：`assets/heightmaps/Hm_Terrain_Peking_513.png` 547x547
- Terrain size：80
- Height scale：12
- Max depth 设置：20
- ROAM 屏幕空间 split/merge 阈值：0.25 px / 0.1 px
- ROAM triangle budget：200000
- DOD 并行 Split：关闭

- CBT capacity：1048576
- CBT triangle area：25 px²
- CBT validation：Off
- CBT geometry：ModifiedOnly

## 总体结果

| Algorithm | Samples | Avg Frame ms | Max Frame ms | Avg LOD ms | Max LOD ms | Avg Triangles | Max Triangles | Avg Nodes | Max Nodes | Avg CPU % | Max CPU % | Max Workers | Config Max Depth | Reached Max Depth | Max Topology Issues |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| Classic CPU ROAM | 64 | 200.22 | 224.80 | 200.25 | 224.07 | 200000.00 | 200000 | 1127313.44 | 1618212 | 98.70 | 106.57 | 1 | 20 | 20 | 0 |
| Data-Oriented CPU ROAM | 64 | 102.34 | 136.35 | 102.51 | 135.64 | 199999.44 | 200000 | 1127303.84 | 1618198 | 161.78 | 432.35 | 8 | 20 | 20 | 0 |
| CBT 2024（GPU 常驻拓扑） | 64 | 0.28 | 2.45 | 0.03 | 0.05 | 28169.78 | 32941 | 28169.78 | 32941 | 0.00 | 0.00 | 0 | 20 | 4 | 0 |

> CPU ROAM 使用边长误差阈值，CBT 使用投影三角形面积阈值；本表可比较固定输入下的绝对开销和规模趋势，但不表示三者已经质量匹配。

## CBT 2024 GPU 阶段

GPU 阶段均来自按交换链帧轮转的 timestamp query，不等待当前帧。汇总只使用计数、compute timestamp 和 terrain draw timestamp 代次一致的新鲜样本。

- 有效对齐样本：64 / 64
- dropped/错代样本：0
- 最大诊断样本年龄：2 帧
- 平均 GPU 阶段合计：0.1104 ms
- 最大 GPU 阶段合计：0.1434 ms
- 最大 BlockingSmoke 等待：0.0000 ms

| CBT GPU 阶段 | Avg ms |
| --- | ---: |
| Classification geometry | 0.0001 |
| Reset | 0.0022 |
| Classify | 0.0085 |
| Split | 0.0101 |
| Allocate | 0.0143 |
| Neighbor copy | 0.0062 |
| Bisect | 0.0065 |
| PropagateBisect | 0.0050 |
| PrepareSimplify | 0.0052 |
| Simplify | 0.0057 |
| PropagateSimplify | 0.0040 |
| ReducePre | 0.0013 |
| ReduceFirst | 0.0018 |
| ReduceSecond | 0.0013 |
| Indexation / indirect | 0.0106 |
| Render vertex evaluation | 0.0098 |
| Validation | 0.0001 |
| Terrain render | 0.0174 |

### CBT 延迟计数

| Fresh samples | Avg active dynamic | Avg remaining dynamic | Avg split candidates | Avg simplify candidates | Avg committed slots | Avg released slots | Max readback B |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 64 | 28163.7812 | 1020412.2188 | 2035.3125 | 2316.7031 | 2282.7812 | 1812.6719 | 424 |

## ROAM 逻辑阶段对比

表中数值均为平均毫秒数。Classic 与 DOD 使用相同的屏幕空间误差、迟滞阈值、活动叶预算和增量 mesh 输出语义。

| ROAM 逻辑阶段 | Classic CPU | DOD CPU | 实现说明 |
| --- | ---: | ---: | --- |
| Prepare / 帧状态 | 5.8800 | 6.4402 | 准备持久拓扑和逐帧输入 |
| Merge 候选评分 | 14.7704 | 2.4605 | 刷新持久 Q_m 的优先级 |
| Merge 拓扑提交 / 向上级联 | 19.3137 | 19.7171 | 提交 diamond merge、邻接修复和级联回收 |
| Split 前 active leaf 收集 | 0.0000 | 0.0000 | 两者均直接复用持久活动叶表示 |
| 视点相关 leaf error / 视锥测试 | 0.0000 | 0.0000 | 计入持久 Q_s 优先级刷新，因此独立字段为 0 |
| Split 扫描/标记 | 42.9353 | 5.5645 | 刷新 Q_s、建堆并生成提交候选 |
| Split 拓扑 / 裂缝约束提交 | 31.9864 | 26.8419 | 按同一预算与 forced-split 约束提交 |
| 细分后 active leaf 收集 / 输出视图 | 0.0000 | 0.0000 | Classic 复用 dense slot owners，DOD 复用 ActiveLeafNodes |
| Mesh emit / draw argument 生成 | 25.1126 | 8.0259 | 两者均只重写 dirty slot；DOD 可将较大批次分段给 worker |
| Finalize / 发布 packet | 56.0339 | 27.7004 | 汇总统计、更新跨帧状态并发布 renderer packet |

## CPU 实现阶段

`CPU update` 包含下表中互斥的物理执行区间；`CPU upload` 是算法返回后的 renderer 上传。Classic 与 DOD 都持久维护 Q_s/Q_m，并在每个 Build 刷新现有成员的优先级。DOD 对两队列的评分并行化，`Split scan/mark` 包含 Q_s 优先级刷新、原地建堆和提交快照生成，`Merge mark` 对应 Q_m 的同类工作。DOD 满预算时持续执行 merge-first 资源交换，直到 max(Q_s) 不再高于 min(Q_m)。两者单独的 `Error eval` 都为零；Classic 与 DOD 的 `Mesh emit` 都是 dirty-slot 增量更新，DOD 对较大的 dirty 批次沿用 worker 分段，因此两者差异不能解释为增量与全量策略差异。

| Algorithm | CPU update | Prepare | Merge mark | Merge topology | Budget leaf collect | Error eval | Split scan/mark | Split topology | Final leaf collect/view | Mesh emit | Finalize | CPU upload |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| Classic CPU ROAM | 196.0340 | 5.8800 | 14.7704 | 19.3137 | 0.0000 | 0.0000 | 42.9353 | 31.9864 | 0.0000 | 25.1126 | 56.0339 | 4.0406 |
| Data-Oriented CPU ROAM | 96.7511 | 6.4402 | 2.4605 | 19.7171 | 0.0000 | 0.0000 | 5.5645 | 26.8419 | 0.0000 | 8.0259 | 27.7004 | 5.5214 |

### CPU Split 拓扑阶段计时（统一口径）

六段均为互斥执行区间。Classic 不执行并行预提交，因此前五项为 0；它与 DOD 的`串行收敛` 都从候选刷新结束后开始，并扣除循环中执行的 merge。`六段合计` 与`Topology total` 的差值是函数调用、worker 数决策和少量循环控制等尚未单列的开销。

| 操作 | 候选排序/分桶 | 队列邻域失效 | chunk 并行提交 | worker 结果汇总 | 活动叶索引/队列刷新 | 串行收敛 | 六段合计 | Topology total |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| Classic Split | 0 | 0 | 0 | 0 | 0 | 31.9864 | 31.9864 | 31.9864 |
| DoD Split | 0.0000 | 0.0000 | 0.0000 | 0.0000 | 0.0000 | 26.8414 | 26.8414 | 26.8419 |
| Merge | 0.1982 | 0.9838 | 0.3470 | 0.0117 | 3.7732 | 14.3907 | 19.7046 | 19.7171 |

### 增量 CPU Mesh 输出

`Full rebuilds` 是采样窗口内的全量初始化次数；其余列是逐帧平均值。Classic 与 DOD 都填充稳定 slot、dirty range 和复用统计。D3D12 的 frame slot 会延迟消费两次使用之间积累的 dirty range 并集，因此 `Max upload bytes` 表示实际补齐量，不要求等于当前 Build 的 updated triangles。

| Algorithm | Full rebuilds | Updated triangles | Reused triangles | Dirty ranges | Max upload bytes |
| --- | ---: | ---: | ---: | ---: | ---: |
| Classic CPU ROAM | 0 | 61645.0781 | 138354.9219 | 24508.7656 | 16196544 |
| Data-Oriented CPU ROAM | 0 | 61640.6094 | 138358.8281 | 35381.6094 | 16196544 |

### 原生 pass 包络

这些是各实现原有的外围 pass 计时。它们与上方互斥阶段重叠，不能重复相加。

| Algorithm | Split | Merge | Emit | Validate |
| --- | ---: | ---: | ---: | ---: |
| Classic CPU ROAM | 74.9217 | 34.0841 | 25.1126 | 0.0000 |
| Data-Oriented CPU ROAM | 44.1813 | 22.1777 | 8.0259 | 0.0000 |
| CBT 2024（GPU 常驻拓扑） | 0.0422 | 0.0149 | 0.0100 | 0.0001 |

## 渲染与上传

| Algorithm | Frame fence wait | Render | Max render | Max upload B | Max readback B |
| --- | ---: | ---: | ---: | ---: | ---: |
| Classic CPU ROAM | 0.00 | 0.77 | 0.97 | 16196544 | 0 |
| Data-Oriented CPU ROAM | 0.00 | 0.74 | 1.02 | 16196544 | 0 |
| CBT 2024（GPU 常驻拓扑） | 0.11 | 0.16 | 0.20 | 0 | 424 |
