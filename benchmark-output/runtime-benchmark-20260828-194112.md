# 运行时基准测试报告

- 相机路径：离散采样点序列；具体路径类型见下方 Benchmark 路径
- 每种算法的路径采样点数：600
- 采样规则：每种算法按相同 sampleIndex 执行全部相机姿态，完成后再切换算法
- 采样完整性：完整
- timeSeconds：当前算法实际经过的墙钟时间，不参与路径推进
- 详细 CSV：`runtime-benchmark-20260828-194112.csv`

- 构建配置：Release
- 图形后端：D3D12
- 图形适配器：NVIDIA GeForce RTX 5090 D (Direct3D 12 (feature level 12_0); requested Agility SDK 614; runtime 6.2.26100.8972 (C:\WINDOWS\SYSTEM32\D3D12Core.dll); driver 32.0.15.9186)
- 算法集合：Classic、Data-Oriented、CBT 2024
- CBT 2024：已纳入相同离散路径和逐阶段 GPU 采样
- Benchmark 路径：默认选项路径
- 每种算法预热帧数：16
- CBT 参数：capacity=128K，area=58.000000 px²，validation=Off，geometry=ModifiedOnly
- 路径采样点数：600；每种算法按相同 sampleIndex 执行完整路径
- CBT 基线标识：cbt-2024-official-baseline-v1
- VSync：基准测试期间已关闭

- Drawable 分辨率：2048x1129
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
| Classic CPU ROAM | 600 | 4.56 | 6.25 | 4.35 | 5.91 | 20000.00 | 20000 | 123954.07 | 224984 | 100.03 | 439.06 | 1 | 20 | 19 | 0 |
| Data-Oriented CPU ROAM | 600 | 2.54 | 9.71 | 2.34 | 9.14 | 19999.42 | 20000 | 123950.77 | 224980 | 249.77 | 7300.12 | 8 | 20 | 19 | 0 |
| CBT 2024 | 600 | 0.32 | 7.07 | 0.04 | 0.12 | 37455.94 | 42947 | 37455.94 | 42947 | 0.00 | 0.00 | 0 | 20 | 20 | 0 |

> CPU ROAM 使用边长误差阈值，CBT 使用投影三角形面积阈值；本表可比较固定输入下的绝对开销和规模趋势，但不表示三者已经质量匹配。

## CBT 2024 GPU 阶段

GPU 阶段均来自按交换链帧轮转的 timestamp query，不等待当前帧。汇总只使用计数、compute timestamp 和 terrain draw timestamp 代次一致的新鲜样本。

- 有效对齐样本：600 / 600
- dropped/错代样本：0
- 最大诊断样本年龄：2 帧
- 平均 GPU 阶段合计：0.0959 ms
- 最大 GPU 阶段合计：0.1295 ms
- 最大 BlockingSmoke 等待：0.0000 ms

| CBT GPU 阶段 | Avg ms |
| --- | ---: |
| Classification geometry | 0.0004 |
| Reset | 0.0022 |
| Classify | 0.0056 |
| Split | 0.0076 |
| Allocate | 0.0080 |
| Neighbor copy | 0.0032 |
| Bisect | 0.0047 |
| PropagateBisect | 0.0044 |
| PrepareSimplify | 0.0042 |
| Simplify | 0.0048 |
| PropagateSimplify | 0.0043 |
| ReducePre | 0.0013 |
| ReduceFirst | 0.0019 |
| ReduceSecond | 0.0011 |
| Indexation / indirect | 0.0050 |
| Render vertex evaluation | 0.0051 |
| Validation | 0.0026 |
| Terrain render | 0.0296 |

### CBT 延迟计数

| Fresh samples | Avg active dynamic | Avg remaining dynamic | Avg split candidates | Avg simplify candidates | Avg committed slots | Avg released slots | Max readback B |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 600 | 37449.9400 | 93622.0600 | 183.1967 | 1846.1467 | 207.3900 | 195.9383 | 428 |

## ROAM 逻辑阶段对比

表中数值均为平均毫秒数。Classic 与 DOD 使用相同的屏幕空间误差、迟滞阈值、活动叶预算和增量 mesh 输出语义。

| ROAM 逻辑阶段 | Classic CPU | DOD CPU | 实现说明 |
| --- | ---: | ---: | --- |
| Prepare / 帧状态 | 0.1565 | 0.1573 | 准备持久拓扑和逐帧输入 |
| Merge 候选评分 | 0.6010 | 0.1578 | 刷新持久 Q_m 的优先级 |
| Merge 拓扑提交 / 向上级联 | 0.0939 | 0.1180 | 提交 diamond merge、邻接修复和级联回收 |
| Split 前 active leaf 收集 | 0.0000 | 0.0000 | 两者均直接复用持久活动叶表示 |
| 视点相关 leaf error / 视锥测试 | 0.0000 | 0.0000 | 计入持久 Q_s 优先级刷新，因此独立字段为 0 |
| Split 扫描/标记 | 1.8915 | 0.4106 | 刷新 Q_s、建堆并生成提交候选 |
| Split 拓扑 / 裂缝约束提交 | 0.2549 | 0.2469 | 按同一预算与 forced-split 约束提交 |
| 细分后 active leaf 收集 / 输出视图 | 0.0000 | 0.0000 | Classic 复用 dense slot owners，DOD 复用 ActiveLeafNodes |
| Mesh emit / draw argument 生成 | 0.1871 | 0.1512 | 两者均只重写 dirty slot；DOD 可将较大批次分段给 worker |
| Finalize / 发布 packet | 1.0443 | 0.9080 | 汇总统计、更新跨帧状态并发布 renderer packet |

## CPU 实现阶段

`CPU update` 包含下表中互斥的物理执行区间；`CPU upload` 是算法返回后的 renderer 上传。Classic 与 DOD 都持久维护 Q_s/Q_m，并在每个 Build 刷新现有成员的优先级。DOD 对两队列的评分并行化，`Split scan/mark` 包含 Q_s 优先级刷新、原地建堆和提交快照生成，`Merge mark` 对应 Q_m 的同类工作。DOD 满预算时持续执行 merge-first 资源交换，直到 max(Q_s) 不再高于 min(Q_m)。两者单独的 `Error eval` 都为零；Classic 与 DOD 的 `Mesh emit` 都是 dirty-slot 增量更新，DOD 对较大的 dirty 批次沿用 worker 分段，因此两者差异不能解释为增量与全量策略差异。

| Algorithm | CPU update | Prepare | Merge mark | Merge topology | Budget leaf collect | Error eval | Split scan/mark | Split topology | Final leaf collect/view | Mesh emit | Finalize | CPU upload |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| Classic CPU ROAM | 4.2296 | 0.1565 | 0.6010 | 0.0939 | 0.0000 | 0.0000 | 1.8915 | 0.2549 | 0.0000 | 0.1871 | 1.0443 | 0.1125 |
| Data-Oriented CPU ROAM | 2.1500 | 0.1573 | 0.1578 | 0.1180 | 0.0000 | 0.0000 | 0.4106 | 0.2469 | 0.0000 | 0.1512 | 0.9080 | 0.1868 |

### CPU Split 拓扑阶段计时（统一口径）

六段均为互斥执行区间。Classic 不执行并行预提交，因此前五项为 0；它与 DOD 的`串行收敛` 都从候选刷新结束后开始，并扣除循环中执行的 merge。`六段合计` 与`Topology total` 的差值是函数调用、worker 数决策和少量循环控制等尚未单列的开销。

| 操作 | 候选排序/分桶 | 队列邻域失效 | chunk 并行提交 | worker 结果汇总 | 活动叶索引/队列刷新 | 串行收敛 | 六段合计 | Topology total |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| Classic Split | 0 | 0 | 0 | 0 | 0 | 0.2549 | 0.2549 | 0.2549 |
| DoD Split | 0.0000 | 0.0000 | 0.0000 | 0.0000 | 0.0000 | 0.2467 | 0.2467 | 0.2469 |
| Merge | 0.0054 | 0.0000 | 0.0000 | 0.0000 | 0.0000 | 0.1115 | 0.1169 | 0.1180 |

### 增量 CPU Mesh 输出

`Full rebuilds` 是采样窗口内的全量初始化次数；其余列是逐帧平均值。Classic 与 DOD 都填充稳定 slot、dirty range 和复用统计。D3D12 的 frame slot 会延迟消费两次使用之间积累的 dirty range 并集，因此 `Max upload bytes` 表示实际补齐量，不要求等于当前 Build 的 updated triangles。

| Algorithm | Full rebuilds | Updated triangles | Reused triangles | Dirty ranges | Max upload bytes |
| --- | ---: | ---: | ---: | ---: | ---: |
| Classic CPU ROAM | 0 | 1480.9900 | 18519.0100 | 1030.8933 | 568176 |
| Data-Oriented CPU ROAM | 0 | 1477.0850 | 18522.3317 | 1137.6983 | 567504 |

### 原生 pass 包络

这些是各实现原有的外围 pass 计时。它们与上方互斥阶段重叠，不能重复相加。

| Algorithm | Split | Merge | Emit | Validate |
| --- | ---: | ---: | ---: | ---: |
| Classic CPU ROAM | 2.1465 | 0.6949 | 0.1871 | 0.0000 |
| Data-Oriented CPU ROAM | 0.6579 | 0.2758 | 0.1512 | 0.0000 |
| CBT 2024 | 0.0278 | 0.0133 | 0.0055 | 0.0026 |

## 渲染与上传

| Algorithm | Frame fence wait | Render | Max render | Max upload B | Max readback B |
| --- | ---: | ---: | ---: | ---: | ---: |
| Classic CPU ROAM | 0.00 | 0.03 | 0.06 | 568176 | 0 |
| Data-Oriented CPU ROAM | 0.00 | 0.03 | 0.17 | 567504 | 0 |
| CBT 2024 | 0.12 | 0.12 | 0.41 | 0 | 428 |
