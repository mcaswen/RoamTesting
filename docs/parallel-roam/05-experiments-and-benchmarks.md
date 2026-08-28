# 实验与基准测试

> 状态更新：2026-08-29
>
> 当前主分支的正式运行时对照为 Classic CPU ROAM、Data-Oriented CPU ROAM 和 D3D12 CBT 2024。GPU ROAM-like 只保留在 `archive/gpu-roam-like` 分支，其旧报告不再代表当前实现。

## 1. 当前实验目标

实验同时回答四类问题：

1. 三种算法在相近活动三角形规模下的端到端帧耗时、P95/P99 和稳定性如何；
2. DOD 相对 Classic 的收益来自哪些 CPU 阶段，又在哪些拓扑阶段回吐；
3. CBT 的 GPU 拓扑、几何、诊断和 terrain draw 各自消耗多少时间；
4. 性能差异是否来自相机路径、三角形工作量或错误拓扑，而不是实现本身。

性能结论不等同于画质结论。Classic/DOD 使用嵌套楔形厚度和保守屏幕投影，CBT 使用投影面积分类；即使平均三角形数相近，也需要独立离线质量评估器才能比较屏幕位置、深度和法线误差。

## 2. 三条算法路径

| 算法 | 拓扑与几何位置 | 预算/阈值 | 输出路径 |
| --- | --- | --- | --- |
| Classic CPU ROAM | CPU 指针拓扑、持久双队列、增量 CPU Mesh | 活动叶三角形硬预算；split/merge 像素阈值 | CPU dirty ranges 上传，普通 indexed draw |
| Data-Oriented CPU ROAM | CPU SoA/索引拓扑、批量评分与条件并行 | 与 Classic 相同的硬预算和像素阈值 | 持久增量 CPU Mesh 与 dirty-range upload；不按 dirty 比例自动切换全量 emit |
| CBT 2024 | D3D12 GPU 常驻 OCBT、半边拓扑与高度图几何 | 固定二分器池容量；投影面积阈值 | GPU active index、vertex 和 draw args，`ExecuteIndirect` |

CBT 支持 128K、256K、512K 和 1M 四档容量。容量限制动态二分器槽位，不是逐帧三角形硬预算；极限路径只能通过面积阈值把平均活动规模校准到 CPU 的 200K 附近。

## 3. 固定场景

| 配置 | 默认路径 | 极限路径 |
| --- | ---: | ---: |
| Height Map | Test129，实际 129×129 | Peking，实际 547×547 |
| Terrain size / height scale | 30 / 4 | 80 / 12 |
| Max depth | 20 | 20 |
| CPU split / merge | 4 px / 2 px | 0.25 px / 0.1 px |
| CPU triangle budget | 20,000 | 200,000 |
| CBT capacity / area | 128K / 58 px² | 1M / 2.05 px² |
| 每轮样本 | 每算法 600 | 每算法 64 |
| 预热 | 每算法 16 帧 | 每算法 24 帧 |
| CBT validation / geometry | Off / ModifiedOnly | Off / ModifiedOnly |

每种算法按相同离散 `sampleIndex` 运行完整相机路径。实际墙钟时间只用于计时，不驱动相机，因此算法快慢不会改变采样密度。

## 4. 运行入口

### 4.1 D3D12 三算法 runtime benchmark

```powershell
powershell -ExecutionPolicy Bypass -File scripts/run/d3d12/run_release_fetch.ps1 --runtime-benchmark
```

也可以直接运行已经构建的程序：

```powershell
.\build\release-d3d12-fetch\bin\ParallelROAM.exe --runtime-benchmark
.\build\release-d3d12-fetch\bin\ParallelROAM.exe --runtime-benchmark --runtime-benchmark-path extreme
```

OpenGL 构建只运行 Classic 与 DOD。D3D12 构建在设备通过 Shader Model 6.6、64 位整数和资源原子能力检查后自动加入 CBT。

### 4.2 参数覆盖

默认/极限路径的固定值可以通过以下参数显式覆盖：

```text
--runtime-benchmark-path
--runtime-benchmark-heightmap
--runtime-benchmark-terrain-size
--runtime-benchmark-height-scale
--runtime-benchmark-max-depth
--runtime-benchmark-split-pixels
--runtime-benchmark-merge-pixels
--runtime-benchmark-cbt-area
--runtime-benchmark-cbt-capacity
--runtime-benchmark-cbt-validation
--runtime-benchmark-cbt-geometry
--runtime-benchmark-samples
--runtime-benchmark-label
```

正式横向比较不得在三种算法之间分别修改共享场景、相机、分辨率或样本数。若为 CBT 单独校准面积阈值，报告必须同时列出实际三角形分布。

### 4.3 CBT 官方语义容量矩阵

```powershell
git switch --detach benchmark/cbt-2024-official-baseline-v1
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/benchmark/d3d12/run_cbt_2024_official_baseline_v1.ps1
```

该脚本固定执行 `default/extreme × 128K/256K/512K/1M × 3 repeats`，用于验证容量趋势与冻结身份，不替代三算法最终对比。

## 5. 统计口径

### 5.1 端到端

- `frameMilliseconds`：单个采样帧总墙钟时间；
- 平均、P50、P95、P99、最大值：使用全部有效样本；
- 总帧用时：`frameMilliseconds` 求和，不包含启动、预热和报告写盘；
- 轮间 SD/CV：先求每轮平均，再对各轮均值计算样本标准差和变异系数。

### 5.2 CPU 阶段

`cpuUpdateMs` 是互斥 LOD 阶段之和，主要拆为 Prepare、Merge mark、Merge topology、Split scan、Split topology、Mesh emit 和 Finalize。Upload 位于 CPU update 之外。逻辑 `split/merge/emit` pass 与这些细分字段存在包含关系，不能重复相加。

### 5.3 CBT GPU 阶段

CBT 使用交换链轮转 timestamp 槽延迟发布以下 18 项：

```text
Classification geometry
Reset
Classify
Split
Allocate
Neighbor copy
Bisect
Propagate bisect
Prepare simplify
Simplify
Propagate simplify
Reduce pre
Reduce first
Reduce second
Indexation / indirect
Render geometry
Validation
Terrain render
```

普通性能帧不会为了当前样本同步等待。报告必须同时记录 timing/diagnostic/terrain draw generation、sample age 和 dropped；代次不对齐的样本不得进入统计。

### 5.4 正确性与工作量

每轮至少核对：

```text
invalidTopology == 0
tjunctions == 0
invalidNeighbors == 0
ActiveTriangleCount <= TriangleBudget  # 仅 Classic/DOD
dropped == false                       # CBT 有效延迟样本
topologyGeneration - diagnosticGeneration == sampleAge
```

还需报告平均、P95、P99 和最大活动三角形数，以及 CBT 动态槽位、最大实际深度、split/merge 计数和资源代次。

## 6. 最终实验数据

最终数据来自 2026-08-28 的 Release、D3D12、VSync Off、RTX 5090 D、1280×720 实验。默认和极限路径各五轮，共 10 份报告、9960 行 CSV；默认每算法 3000 个样本，极限每算法 320 个样本。所有算法在五轮中的逐 `sampleIndex` 三角形数量完全确定，所有拓扑问题为零。

完整原始报告索引、P95/P99、轮间稳定性、分阶段表和异常轮分析见[最终实验数据分析与结论](../../benchmark-output/runtime-benchmark-final-analysis-20260828.md)。

### 6.1 端到端结果

| 路径 | 算法 | 平均 ms | P95 ms | P99 ms | 总帧用时 ms | 轮间 CV |
| --- | --- | ---: | ---: | ---: | ---: | ---: |
| 默认 | Classic | 4.720 | 6.639 | 8.634 | 14159.930 | 9.78% |
| 默认 | DOD | 3.486 | 5.112 | 6.250 | 10457.202 | 18.98% |
| 默认 | CBT 2024 | 0.319 | 0.489 | 0.642 | 957.107 | 7.49% |
| 极限 | Classic | 201.846 | 225.289 | 234.723 | 64590.608 | 1.92% |
| 极限 | DOD | 119.017 | 141.885 | 158.894 | 38085.373 | 4.34% |
| 极限 | CBT 2024 | 0.939 | 2.012 | 16.574 | 300.502 | 116.12% |

| 路径 | 对比 | 平均变化 | P95 变化 | P99 变化 |
| --- | --- | ---: | ---: | ---: |
| 默认 | DOD 相对 Classic | -26.15% | -23.00% | -27.61% |
| 默认 | CBT 相对 Classic | -93.24% | -92.63% | -92.56% |
| 极限 | DOD 相对 Classic | -41.04% | -37.02% | -32.31% |
| 极限 | CBT 相对 Classic | -99.53% | -99.11% | -92.94% |

极限 CBT 的 pooled P99 受第 1 轮异常影响。第 2-5 轮合并后，CBT 平均为 0.452 ms、P95 为 0.692 ms、P99 为 0.996 ms，轮间 CV 为 5.08%。正式结论保留异常轮，同时给出该敏感性分析，不选择性删样。

### 6.2 三角形工作量

| 路径 | Classic 平均 | DOD 平均 | CBT 平均 | CBT 相对 CPU |
| --- | ---: | ---: | ---: | ---: |
| 默认 | 18758.02 | 18758.02 | 18732.14 | -0.138% |
| 极限 | 200000.00 | 199999.44 | 201811.17 | +0.906% |

默认路径已经近乎完全对齐。极限 CBT 没有 200K 硬预算，活动数量在 182953-216723 之间随相机和面积阈值变化。

### 6.3 Classic 与 DOD 分阶段

| 路径 | 阶段 | Classic avg ms | DOD avg ms | DOD 变化 |
| --- | --- | ---: | ---: | ---: |
| 默认 | CPU update | 4.3460 | 3.1223 | -28.16% |
| 默认 | Merge mark | 0.6108 | 0.2348 | -61.56% |
| 默认 | Merge topology | 0.0942 | 0.1692 | +79.65% |
| 默认 | Split scan | 1.9273 | 0.5894 | -69.42% |
| 默认 | Split topology | 0.2099 | 0.3127 | +48.99% |
| 默认 | Mesh emit | 0.1915 | 0.2355 | +23.02% |
| 极限 | CPU update | 197.3843 | 112.2686 | -43.12% |
| 极限 | Merge mark | 15.0103 | 2.8396 | -81.08% |
| 极限 | Merge topology | 18.7172 | 23.2098 | +24.00% |
| 极限 | Split scan | 47.0477 | 6.7338 | -85.69% |
| 极限 | Split topology | 32.5351 | 32.7184 | +0.56% |
| 极限 | Mesh emit | 27.2397 | 11.6421 | -57.26% |

DOD 的主要收益来自连续数据上的候选扫描；拓扑提交并未普遍快于 Classic。极限路径的 DOD split topology 几乎全部消耗在串行收敛，merge topology 中串行收敛约占 74.44%，这是后续 CPU 优化的明确目标。

### 6.4 CBT GPU 分阶段

| 路径 | GPU 阶段和 avg / P95 / P99 ms | 最大平均阶段 | 占比 |
| --- | --- | --- | ---: |
| 默认 | 0.07864 / 0.083 / 0.088 | Terrain render 0.01563 ms | 19.87% |
| 极限 | 0.33376 / 1.182 / 1.938 | Terrain render 0.12848 ms | 38.50% |

极限路径中其次是 Classify 11.91%、Render geometry 6.90%、Allocate 6.24%、Indexation 5.91% 和 Neighbor copy 5.62%。第 2-5 轮的 GPU 阶段和平均为 0.17315 ms；第 1 轮同时出现 render、fence 和 GPU timing 异常。

CPU 算法在极限路径平均每帧上传约 18.45 MB。CBT 直接消费 GPU 常驻拓扑和 indirect draw state，没有 CPU Mesh upload，每帧诊断 readback 固定为 428 B。这是 CBT 端到端优势远大于单一 topology pass 差距的重要原因。

## 7. 结果解释边界

- CPU wall time 与 GPU timestamp 可以比较端到端实现成本，不能解释为相同处理器上的指令效率；
- CBT timestamp 延迟两帧发布，单行 GPU 阶段、fence 和当前 `frameMilliseconds` 不能逐行相加；
- 初始化、地形重载和资源重建不属于稳定帧表，需要独立 initialization benchmark；
- 单轮数据不足以冻结结论，正式实验至少运行五轮并报告 pooled P95/P99 与轮间 CV；
- 面积阈值校准只对齐工作规模，不证明三算法生成相同空间误差；
- 旧 GPU ROAM-like、旧误差公式和旧分辨率报告只用于历史追溯，不得与最终数据混合。

## 8. 当前结论

在最终测试环境和已校准工作量下，性能层级稳定为 CBT、DOD、Classic。DOD 证明数据导向布局和批量评分能显著压缩扫描成本，但串行 topology commit 仍限制进一步收益；CBT 将分类、动态拓扑、几何和绘制参数保持在 GPU，避免了大规模 CPU 遍历与 CPU Mesh 上传，因此在默认和极限路径都形成数量级优势。

下一阶段的研究重点不再是继续调整 CBT v1 的面积参数，而是建立公共质量评估器，并在独立算法身份中验证兼容闭包感知的严格预算调度。
