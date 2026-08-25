# CBT 2024 官方语义基线 v1

> 冻结日期：2026-08-25
> 基线标识：`cbt-2024-official-baseline-v1`
> 算法键：`cbt_2024_official_baseline_v1`
> Git 提交：`b0bdd5d0c25a523f5a15221dfb90eaf4db829b4c`
> Benchmark 标签：`benchmark/cbt-2024-official-baseline-v1`

## 1. 冻结范围与复现入口

阶段 I 将 CBT 基线的算法身份、shader、PSO 入口、CPU/GPU ABI、两条离散相机路径、地形输入、分辨率、四档容量和采样口径冻结在同一个清单中：

- 清单：[`baselines/cbt-2024-official-v1.json`](baselines/cbt-2024-official-v1.json)；
- 正式运行器：[`run_cbt_2024_official_baseline_v1.ps1`](../../scripts/benchmark/d3d12/run_cbt_2024_official_baseline_v1.ps1)；
- 汇总结果：[`README.md`](../../benchmark-output/cbt-2024-official-baseline-v1/README.md)、[`capacity-summary.csv`](../../benchmark-output/cbt-2024-official-baseline-v1/capacity-summary.csv) 和 [`run-summary.csv`](../../benchmark-output/cbt-2024-official-baseline-v1/run-summary.csv)；
- 第三方权利记录：[`THIRD_PARTY_NOTICES.md`](../../THIRD_PARTY_NOTICES.md)。

清单为 55 个关键源码、shader、场景资产和运行脚本保存规范化 SHA-256。文本先统一为 UTF-8/LF 再计算，二进制高度图按原始字节计算，因此 Windows 的 CRLF 检出不会制造伪漂移。正式模式还要求当前 `HEAD` 与 benchmark 标签指向同一提交，并且 tracked worktree 干净。

在仓库根目录执行：

```powershell
git switch --detach benchmark/cbt-2024-official-baseline-v1
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/benchmark/d3d12/run_cbt_2024_official_baseline_v1.ps1
```

仅验证提交、标签和冻结输入：

```powershell
git switch --detach benchmark/cbt-2024-official-baseline-v1
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/benchmark/d3d12/run_cbt_2024_official_baseline_v1.ps1 -VerifyOnly
```

运行器固定执行 `default/extreme × 128K/256K/512K/1M × 3 repeats`，每次只测 CBT，性能模式为 `validation=Off`、`geometry=ModifiedOnly`、`VSync=Off`。逐帧原始报告保存在 Git 忽略的 `raw/<session>`，跟踪的每轮汇总保存原始 CSV 的 SHA-256。

## 2. 固定实验身份

| 项目 | 固定值 |
|---|---|
| 构建与后端 | D3D12 / RelWithDebInfo / Shader Model 6.6 |
| 分辨率 | 1280 × 720 drawable |
| 工具链 | Agility SDK 1.614.1，DXC 1.7.2308.12 |
| GPU | NVIDIA GeForce RTX 5090 D |
| D3D12 运行时 | feature level 12_0，D3D12Core 6.2.26100.8972，driver 32.0.15.9186 |
| 默认路径 | Test129 实际 129 × 129，terrain 30，height 4，depth 20，58 px²，600 点，预热 16 帧 |
| 极限路径 | Peking 文件名含 513、实际解码 547 × 547，terrain 80，height 12，depth 20，2.05 px²，64 点，预热 24 帧 |
| 容量 | 128K、256K、512K、1M |

分辨率、实际高度图尺寸和每个离散 `sampleIndex` 都由运行器核对；报告出现错误后不会继续生成一个表面完整的汇总。

## 3. 四档容量结果

每个表项是三轮独立进程的平均值；`Repeat SD` 是三轮均值之间的样本标准差。GPU 时间为 18 个 CBT compute/geometry/validation 阶段之和，不包含 terrain draw，也不包含 CPU 围栏等待。

| 路径 | 容量 | 每轮样本 | 平均三角形 | 三角形 Repeat SD | GPU 阶段 ms | GPU Repeat SD | 平均剩余动态槽 | 最低剩余槽 | 最大活动深度 | 拓扑问题 |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| default | 128K | 600 | 18732.1417 | 0.0000 | 0.0834 | 0.0008 | 112345.8583 | 111050 | 20 | 0 |
| default | 256K | 600 | 18732.1417 | 0.0000 | 0.0844 | 0.0000 | 243417.8583 | 242122 | 20 | 0 |
| default | 512K | 600 | 18732.1417 | 0.0000 | 0.0878 | 0.0008 | 505561.8583 | 504266 | 20 | 0 |
| default | 1M | 600 | 18732.1417 | 0.0000 | 0.0938 | 0.0004 | 1029849.8583 | 1028554 | 20 | 0 |
| extreme | 128K | 64 | 120771.5573 | 54.5173 | 0.1470 | 0.0002 | 10306.4427 | 355 | 20 | 0 |
| extreme | 256K | 64 | 201811.1719 | 0.0000 | 0.1588 | 0.0005 | 60338.8281 | 45427 | 20 | 0 |
| extreme | 512K | 64 | 201811.1719 | 0.0000 | 0.1639 | 0.0006 | 322482.8281 | 307571 | 20 | 0 |
| extreme | 1M | 64 | 201811.1719 | 0.0000 | 0.1694 | 0.0002 | 846770.8281 | 831859 | 20 | 0 |

结果表现出两种可解释的占用趋势：

1. 默认路径不受容量限制，四档容量的活动三角形轨迹完全一致；容量增加只提高空闲槽数，并让全容量 Reduce/Indexation 的时间缓慢上升。
2. 极限路径在 128K 下接近饱和，最低只剩 355 个动态槽，因此平均规模约 12.08 万且三轮存在小幅原子调度差异；256K 起有足够空间，三角形轨迹稳定在约 20.18 万，继续增加容量不改变拓扑结果。

这与上游 OCBT 的容量语义一致：容量只限制可驻留动态 bisector 的数量，不是三角形预算本身。128K 压力结果不得拿来声称面积阈值失效，256K/512K/1M 的一致结果才是该极限路径的未饱和参考。

## 4. 与上游实现的语义对照

上游官方参考固定为 `AnisB/large_cbt@7351e6fb380acc149b3aef22a6c39bf3df7950a6`。本机研究 checkout 的 `7ae736d179528a0996449c0cc2db7f3279edc8ee` 只替换 NVIDIA 不接受的 64 位 `firstbithigh` 写法，不改变 CBT 调度或拓扑语义。

### 4.1 拓扑 pass 顺序

两边的正式拓扑事务保持下列顺序：

```text
Reset
Classify
PrepareIndirect(split) -> Split
PrepareIndirect(allocate) -> Allocate
neighbor copy
Bisect
PrepareIndirect(propagate split) -> PropagateBisect
PrepareSimplify
PrepareIndirect(simplify) -> Simplify
PrepareIndirect(propagate simplify) -> PropagateSimplify
ReducePre -> ReduceFirst -> ReduceSecond
neighbor generation swap
Indexation -> PrepareIndirect(draw/dispatch)
```

RoamTesting 在这条拓扑事务前按需增加 classification geometry 重建，在 Indexation 后增加 modified/full 高度图几何求值与诊断复制；这些 pass 为高度图适配和观测层，不改变上面 split/merge/Reduce 的先后关系。两边的邻接整表复制都使用 D3D12 copy 路径，随后只在 next generation 上提交局部修改。

### 4.2 明确保留的项目差异

| 维度 | 上游 `large_cbt` | RoamTesting 基线 |
|---|---|---|
| 基础网格 | `icosahedron.ccm` 球体基础网格，20 面/60 半边，基础深度 6 | 方形地形由两个三角形展开为 6 个基础半边，基础深度 3 |
| 几何 | `PlanetGeometry.compute`、球面 LEB 和行星位移 | 高度图双线性采样，输出位置、法线、UV、调试色及父级分类位置 |
| 默认窗口/容量 | 1920 × 1080、OCBT 128K、triangle area 60 | 正式路径固定 1280 × 720；四容量矩阵；58/2.05 px²分别校准默认/极限路径 |
| 绘制集合 | 生成 active/visible/modified，默认仍绘制 active | 保持相同 active draw 语义，renderer 由 GPU `ExecuteIndirect` 的 draw state 决定数量 |
| 帧围栏 | demo 每帧 present 后 flush 队列，再处理 profiling/validation/occupancy | 普通帧使用轮转 readback/timestamp 槽，不等待当前帧；仅重建和 `BlockingSmoke` 显式等待 |
| 验证 | 可选全容量邻接验证，flush 后 CPU 断言 | Off/Delayed/BlockingSmoke；额外核对 OCBT 根、bit/slot/heapID、索引/间接参数、容量守恒、split/merge 计数和基础几何 |

因此本基线冻结的是上游的 CBT 拓扑语义，而不是上游 `outer_space` 的球体场景或每帧同步策略。两种程序之间不能直接用绝对三角形数或绝对帧时间宣称数值等价；可比较项是 pass 顺序、容量饱和趋势、拓扑不变量和验证结果。

## 5. 验证与同步结论

正式性能矩阵使用 `Off`，但仍保留活动数、最大活动深度、资源代次和轻量问题计数的延迟读回。24 轮运行的所有样本均满足：

- diagnostic、GPU timing 与 terrain draw 来自同一已完成代次；
- `topologyGeneration - diagnosticGeneration == sampleAge`；
- `dropped=false`；
- `maxTopologyIssues=0`；
- 最大活动深度真实达到 20，而不是基础深度或固定常量。

独立 D3D12 smoke 会在同一次 540 帧运行中覆盖高度图/容量切换、远近 split/merge、Delayed、BlockingSmoke 和 Off。阶段 I 验收中四档容量 smoke 及 runtime quick 全部通过；RelWithDebInfo D3D12 为 26/26 CTest，OpenGL 公共回归为 18/18 CTest。shader 已计入注释率门禁。

## 6. 许可证门禁

截至 2026-08-25，上游仓库没有可识别的 `LICENSE`、`COPYING`、SPDX 声明或 README 许可证条款。阶段 I 因此只能完成技术基线冻结，不能把“公开可读源码”解释为“获得复制、修改或再分发许可”。

在上游作者提供兼容许可证或书面授权前：

- `third_party/large_cbt` 不进入发布包；
- CBT source-referenced 部分的公开再分发保持阻塞；
- 本清单、测试和结果仅记录本地研究复现状态，不构成授权结论。

## 7. 阶段 I 结论

阶段 I 的技术条件已经满足：后续研究变体可以从稳定算法键、固定标签、冻结输入、双路径四容量三次重复结果和无普通帧 CPU 同步的统计链路出发。许可证条件没有被假定为通过，而是转化为显式发布门禁；在授权完成前，基线可用于本地比较，不能作为可公开分发的上游衍生发布物。
