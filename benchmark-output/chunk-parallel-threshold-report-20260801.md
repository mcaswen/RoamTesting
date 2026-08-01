# DOD Chunk 并行提交候选阈值实验

## 结论

- **Merge chunk commit：** 保守交叉点约为 `160` 个 interior candidates。`150-159` 已开始出现收益，但稳定性低于 `160+`；建议后续把 Merge 并行阈值从当前 `32` 调整到 `160`。
- **Split chunk commit：** 当前三个内置 profile 中，单帧最多只有 `17` 个安全 interior candidates。`2-17` 区间的并行中位耗时比串行高 `0.0180 ms`，并行胜率只有 `37.5%`。当前证据支持保持串行，尚无法推断更大 Split batch 的交叉点。
- 候选数不是唯一变量。`nonEmptyChunkCount` 决定实际 worker 数；例如同为 `140` candidates，7 个 chunk 的样本回退 `7.3%`，9 个 chunk 的样本提升 `5.5%`。`160` 是针对当前 `8x8` chunk、最多 8 workers 和测试机器的保守一维阈值。
- 本轮只验证阈值，**没有修改产品默认值**。

## 测试环境

- 日期：2026-08-01
- 构建：OpenGL RelWithDebInfo
- CPU：`AMD64 Family 26 Model 68 Stepping 0, AuthenticAMD`，32 logical processors（环境未暴露零售型号）
- GPU/驱动：NVIDIA GeForce RTX 5090 D，591.86
- CPU worker：自动模式，最多 8
- Chunk 网格：`8x8`
- Standard：Peking 513，高度缩放 4，Max depth 14，Split/Merge 4 px / 2 px，Triangle budget 20000，固定 64 帧轨迹
- Smoke：Test129，同一组 ROAM 参数

## 方法

先用全串行 topology commit 重放固定轨迹，选取覆盖候选规模的目标 Build。对每个目标点分别启动独立进程：

1. 目标 Build 之前的所有帧强制串行，保证进入目标 Build 前的持久拓扑相同；
2. 只在目标 Build 的目标阶段切换 serial/parallel，Split 与 Merge 分开测；
3. 每点执行 12 对，serial/parallel 交替先后顺序；
4. 检查每一对的 candidates、non-empty chunks、triangles、split/merge count 完全相同；
5. 共得到 1,224 个目标帧样本，所有样本 PASS，拓扑错误均为 0；
6. 所有并行样本的 `parallel commit count == candidate count`，没有候选在 chunk commit 中被丢弃。

实验覆盖通过环境变量控制，未设置时保持产品默认行为：

- `PARALLEL_ROAM_DOD_MIN_PARALLEL_COMMIT_CANDIDATES`：覆盖最小 candidate 数；
- `PARALLEL_ROAM_DOD_PARALLEL_COMMIT_BUILD`：只允许指定 BuildSequence 并行；
- `PARALLEL_ROAM_DOD_PARALLEL_COMMIT_PHASE`：选择 `split`、`merge` 或默认 `both`。

## Merge 配对结果

`pairedDelta = parallel - serial`，负值表示并行更快。

| Candidate 区间 | 配对样本 | Serial 中位 ms | Parallel 中位 ms | Paired delta 中位 ms | 并行胜率 | 判断 |
| --- | ---: | ---: | ---: | ---: | ---: | --- |
| `<50` | 60 | 0.0618 | 0.0878 | +0.0250 | 10.0% | 串行 |
| `50-99` | 96 | 0.1846 | 0.1952 | +0.0061 | 44.8% | 串行 |
| `100-149` | 120 | 0.2754 | 0.2728 | -0.0039 | 55.8% | 噪声/不稳定区 |
| `150-159` | 24 | 0.3719 | 0.3560 | -0.0261 | 70.8% | 交叉区 |
| `160-199` | 84 | 0.3660 | 0.3182 | -0.0432 | 79.8% | 并行 |
| `200-399` | 60 | 0.4732 | 0.3830 | -0.0928 | 86.7% | 并行 |
| `400+` | 24 | 2.9288 | 2.7200 | -0.2045 | 83.3% | 并行 |

单点结果也支持这个边界：`150` candidates 提升 `9.8%`，`158` 提升 `7.8%`；从 `166` 到 `181` 的全部代表点均提升 `6.5%-15.1%`。`203-320` 提升 `8.5%-34.4%`。Smoke 的 `644/1777` candidates 分别提升 `3.9%/10.4%`。

## Split 配对结果

| Candidate 区间 | 配对样本 | Serial 中位 ms | Parallel 中位 ms | Paired delta 中位 ms | 并行胜率 | 判断 |
| --- | ---: | ---: | ---: | ---: | ---: | --- |
| `2-17` | 144 | 0.4548 | 0.4840 | +0.0180 | 37.5% | 保持串行 |

Standard 的 Split interior batch 为 `0-17`，Smoke 为 0，Budget-reentry 最大为 3。只有一个 `17 candidates / 6 chunks` 单点显示约 `10.7%` 收益，不能据此建立阈值；需要构造能稳定产生更大安全 Split batch 的专用场景后重新测量。

## 全轨迹确认

对 Standard 64 帧轨迹分别使用阈值 `32/150/160/192/强制串行`，每种策略运行 20 轮。所有组的平均 triangles 中位数均为 `18489.02`，拓扑错误为 0。

| 阈值 | CPU update 中位 ms | Merge topology 中位 ms | Split topology 中位 ms | 并行 Merge 帧 | 并行 Split 帧 |
| ---: | ---: | ---: | ---: | ---: | ---: |
| 32 | 6.4285 | 0.5178 | 0.5433 | 58 | 0 |
| 150 | 6.5528 | 0.5244 | 0.5426 | 29 | 0 |
| 160 | 6.9082 | 0.5361 | 0.5647 | 27 | 0 |
| 192 | 6.5746 | 0.5338 | 0.5458 | 19 | 0 |
| 强制串行 | 6.6877 | 0.5614 | 0.5507 | 0 | 0 |

全轨迹结果受不同阈值造成的持久拓扑提交顺序和系统噪声影响，不能用于定位交叉点；它只能作为无明显正确性回退的补充。严格配对实验才是本报告阈值结论的主要证据。

## 限制

- 结论适用于当前 CPU、线程池、`8x8` chunk 和最多 8 workers；换 CPU 或 chunk 网格后需要复测。
- 当前阈值只看 candidate count。更精确的策略可同时考虑 `nonEmptyChunkCount`，例如估算 `candidateCount / workerCount`。
- Merge topology 计时包含并行预提交后的串行动态队列收敛，因此这里测量的是完整 ROAM Merge topology 阶段收益，而不是孤立 worker kernel。
- Split 缺少大 batch 样本，不能声称其真实交叉点高于 17 或等于 Merge 的 160。
