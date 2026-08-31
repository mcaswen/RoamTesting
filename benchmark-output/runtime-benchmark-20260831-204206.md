# 运行时基准测试报告

- 相机路径：离散采样点序列；具体路径类型见下方 Benchmark 路径
- 每种算法的路径采样点数：64
- 采样规则：每种算法按相同 sampleIndex 执行全部相机姿态，完成后再切换算法
- 采样完整性：完整
- timeSeconds：当前算法实际经过的墙钟时间，不参与路径推进
- 详细 CSV：`runtime-benchmark-20260831-204206.csv`

- 构建配置：Release
- 图形后端：D3D12
- 图形适配器：NVIDIA GeForce RTX 5090 D (Direct3D 12 (feature level 12_0); requested Agility SDK 614; runtime 6.2.26100.8972 (C:\WINDOWS\SYSTEM32\D3D12Core.dll); driver 32.0.15.9186)
- 算法集合：Classic、Data-Oriented、CBT 2024
- CBT 2024：已纳入相同离散路径和逐阶段 GPU 采样
- Benchmark 路径：极限压力路径
- 每种算法预热帧数：24
- CBT 参数：capacity=1M，area=2.050000 px²，validation=Off，geometry=ModifiedOnly
- 路径采样点数：64；每种算法按相同 sampleIndex 执行完整路径
- CBT 基线标识：cbt-2024-official-baseline-v1
- VSync：基准测试期间已关闭

- Drawable 分辨率：2048x1129
- Height map：`assets/heightmaps/Hm_Terrain_Peking_513.png` 547x547
- Terrain size：80
- Height scale：12
- Max depth 设置：20
- ROAM 屏幕空间 split/merge 阈值：0.25 px / 0.1 px
- ROAM triangle budget：200000
- DOD 并行 Split：关闭

- CBT capacity：1048576
- CBT triangle area：2.05 px²
- CBT validation：Off
- CBT geometry：ModifiedOnly

## 总体结果

| Algorithm | Samples | Avg Frame ms | Max Frame ms | Avg LOD ms | Max LOD ms | Avg Triangles | Max Triangles | Avg Nodes | Max Nodes | Avg CPU % | Max CPU % | Max Workers | Config Max Depth | Reached Max Depth | Max Topology Issues |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| Classic CPU ROAM | 64 | 201.29 | 244.46 | 201.96 | 243.65 | 200000.00 | 200000 | 1125600.03 | 1614326 | 100.87 | 110.16 | 1 | 20 | 20 | 0 |
| Data-Oriented CPU ROAM | 64 | 109.63 | 142.03 | 109.98 | 141.34 | 199999.55 | 200000 | 1125597.62 | 1614322 | 175.48 | 407.54 | 8 | 20 | 20 | 0 |
| CBT 2024 | 64 | 0.91 | 9.67 | 0.04 | 0.15 | 296197.22 | 314328 | 296197.22 | 314328 | 0.00 | 0.00 | 0 | 20 | 20 | 0 |

> CPU ROAM 使用边长误差阈值，CBT 使用投影三角形面积阈值；本表可比较固定输入下的绝对开销和规模趋势，但不表示三者已经质量匹配。

## CBT 2024 GPU 阶段

GPU 阶段均来自按交换链帧轮转的 timestamp query，不等待当前帧。汇总只使用计数、compute timestamp 和 terrain draw timestamp 代次一致的新鲜样本。

- 有效对齐样本：64 / 64
- dropped/错代样本：0
- 最大诊断样本年龄：2 帧
- 平均 GPU 阶段合计：0.5827 ms
- 最大 GPU 阶段合计：7.8077 ms
- 最大 BlockingSmoke 等待：0.0000 ms

| CBT GPU 阶段 | Avg ms |
| --- | ---: |
| Classification geometry | 0.0002 |
| Reset | 0.0119 |
| Classify | 0.0745 |
| Split | 0.0152 |
| Allocate | 0.0203 |
| Neighbor copy | 0.0442 |
| Bisect | 0.0095 |
| PropagateBisect | 0.0078 |
| PrepareSimplify | 0.0065 |
| Simplify | 0.0078 |
| PropagateSimplify | 0.0075 |
| ReducePre | 0.0040 |
| ReduceFirst | 0.0039 |
| ReduceSecond | 0.0024 |
| Indexation / indirect | 0.0345 |
| Render vertex evaluation | 0.0221 |
| Validation | 0.0016 |
| Terrain render | 0.3087 |

### CBT 延迟计数

| Fresh samples | Avg active dynamic | Avg remaining dynamic | Avg split candidates | Avg simplify candidates | Avg committed slots | Avg released slots | Max readback B |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 64 | 296191.2188 | 752384.7812 | 4011.7344 | 5996.8906 | 4688.1875 | 4704.2812 | 428 |

## ROAM 逻辑阶段对比

表中数值均为平均毫秒数。Classic 与 DOD 使用相同的屏幕空间误差、迟滞阈值、活动叶预算和增量 mesh 输出语义。

| ROAM 逻辑阶段 | Classic CPU | DOD CPU | 实现说明 |
| --- | ---: | ---: | --- |
| Prepare / 帧状态 | 6.5067 | 7.0176 | 准备持久拓扑和逐帧输入 |
| Merge 候选评分 | 13.7129 | 2.9331 | 刷新持久 Q_m 的优先级 |
| Merge 拓扑提交 / 向上级联 | 16.5992 | 22.2916 | 提交 diamond merge、邻接修复和级联回收 |
| Split 前 active leaf 收集 | 0.0000 | 0.0000 | 两者均直接复用持久活动叶表示 |
| 视点相关 leaf error / 视锥测试 | 0.0000 | 0.0000 | 计入持久 Q_s 优先级刷新，因此独立字段为 0 |
| Split 扫描/标记 | 50.2535 | 7.0679 | 刷新 Q_s、建堆并生成提交候选 |
| Split 拓扑 / 裂缝约束提交 | 27.6969 | 27.6870 | 按同一预算与 forced-split 约束提交 |
| 细分后 active leaf 收集 / 输出视图 | 0.0000 | 0.0000 | Classic 复用 dense slot owners，DOD 复用 ActiveLeafNodes |
| Mesh emit / draw argument 生成 | 27.1419 | 9.6291 | 两者均只重写 dirty slot；DOD 可将较大批次分段给 worker |
| Finalize / 发布 packet | 55.2172 | 27.1042 | 汇总统计、更新跨帧状态并发布 renderer packet |

## CPU 实现阶段

`CPU update` 包含下表中互斥的物理执行区间；`CPU upload` 是算法返回后的 renderer 上传。Classic 与 DOD 都持久维护 Q_s/Q_m，并在每个 Build 刷新现有成员的优先级。DOD 对两队列的评分并行化，`Split scan/mark` 包含 Q_s 优先级刷新、原地建堆和提交快照生成，`Merge mark` 对应 Q_m 的同类工作。DOD 满预算时持续执行 merge-first 资源交换，直到 max(Q_s) 不再高于 min(Q_m)。两者单独的 `Error eval` 都为零；Classic 与 DOD 的 `Mesh emit` 都是 dirty-slot 增量更新，DOD 对较大的 dirty 批次沿用 worker 分段，因此两者差异不能解释为增量与全量策略差异。

| Algorithm | CPU update | Prepare | Merge mark | Merge topology | Budget leaf collect | Error eval | Split scan/mark | Split topology | Final leaf collect/view | Mesh emit | Finalize | CPU upload |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| Classic CPU ROAM | 197.1298 | 6.5067 | 13.7129 | 16.5992 | 0.0000 | 0.0000 | 50.2535 | 27.6969 | 0.0000 | 27.1419 | 55.2172 | 4.6381 |
| Data-Oriented CPU ROAM | 103.7311 | 7.0176 | 2.9331 | 22.2916 | 0.0000 | 0.0000 | 7.0679 | 27.6870 | 0.0000 | 9.6291 | 27.1042 | 5.9756 |

### CPU Split 拓扑阶段计时（统一口径）

六段均为互斥执行区间。Classic 不执行并行预提交，因此前五项为 0；它与 DOD 的`串行收敛` 都从候选刷新结束后开始，并扣除循环中执行的 merge。`六段合计` 与`Topology total` 的差值是函数调用、worker 数决策和少量循环控制等尚未单列的开销。

| 操作 | 候选排序/分桶 | 队列邻域失效 | chunk 并行提交 | worker 结果汇总 | 活动叶索引/队列刷新 | 串行收敛 | 六段合计 | Topology total |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| Classic Split | 0 | 0 | 0 | 0 | 0 | 27.6969 | 27.6969 | 27.6969 |
| DoD Split | 0.0000 | 0.0000 | 0.0000 | 0.0000 | 0.0000 | 27.6865 | 27.6865 | 27.6870 |
| Merge | 0.2306 | 1.0698 | 0.3726 | 0.0171 | 4.3394 | 16.2206 | 22.2502 | 22.2916 |

### 增量 CPU Mesh 输出

`Full rebuilds` 是采样窗口内的全量初始化次数；其余列是逐帧平均值。Classic 与 DOD 都填充稳定 slot、dirty range 和复用统计。D3D12 的 frame slot 会延迟消费两次使用之间积累的 dirty range 并集，因此 `Max upload bytes` 表示实际补齐量，不要求等于当前 Build 的 updated triangles。

| Algorithm | Full rebuilds | Updated triangles | Reused triangles | Dirty ranges | Max upload bytes |
| --- | ---: | ---: | ---: | ---: | ---: |
| Classic CPU ROAM | 0 | 81726.8906 | 118273.1094 | 27797.6094 | 20126568 |
| Data-Oriented CPU ROAM | 0 | 81726.4688 | 118273.0781 | 40315.0000 | 20126064 |

### 原生 pass 包络

这些是各实现原有的外围 pass 计时。它们与上方互斥阶段重叠，不能重复相加。

| Algorithm | Split | Merge | Emit | Validate |
| --- | ---: | ---: | ---: | ---: |
| Classic CPU ROAM | 77.9503 | 30.3121 | 27.1419 | 0.0000 |
| Data-Oriented CPU ROAM | 47.4389 | 25.2247 | 9.6291 | 0.0000 |
| CBT 2024 | 0.0970 | 0.0217 | 0.0223 | 0.0016 |

## 渲染与上传

| Algorithm | Frame fence wait | Render | Max render | Max upload B | Max readback B |
| --- | ---: | ---: | ---: | ---: | ---: |
| Classic CPU ROAM | 0.00 | 1.07 | 2.67 | 20126568 | 0 |
| Data-Oriented CPU ROAM | 0.00 | 1.20 | 2.75 | 20126064 | 0 |
| CBT 2024 | 0.65 | 0.61 | 7.93 | 0 | 428 |
