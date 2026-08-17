# C1 阶段验收报告

## 1. 结论

C1/O3 已完成并通过验收。活动叶视图已经与持久 split heap 分离，heapify 不再改变最终活动叶输出顺序；独立 Q_s 的计数、反向位置和拓扑事务维护保持正确。性能上 split heap 的连续 entry 已降低默认路径和高预算压力路径的 split 成本，C1 不触发回退条件，下一步进入 C2/O8。

## 2. 代码与验证范围

- `ActiveLeafNodes`：只由 split/merge 拓扑事务做稠密 swap-remove；
- `SplitQueue`：连续保存 `Score` 和 `Node`，按 heap position 直接比较；
- `SplitQueuePositions`：按 node index 定位 Q_s 项，支持 forced split 任意位置删除；
- priority refresh：只重排 Q_s；验证模式会比较 refresh 前后的活动叶顺序；
- GPU score mirror：仅 GPU ROAM-like 路径启用，CPU DOD 不支付额外随机写入；
- 回归：新增 `roam_split_queue_view_dod`，同时保留 DOD budget-reentry 和拓扑 validator。

## 3. 默认路径

输入为 PGM 129x129、600 个离散相机样本、20,000 triangle budget，结果见 [runtime-benchmark-20260817-230250.md](runtime-benchmark-20260817-230250.md) 和 [CSV](runtime-benchmark-20260817-230250.csv)。

| 指标 | Classic | DOD |
| --- | ---: | ---: |
| 平均 CPU update (ms) | 1.5251 | 1.5998 |
| 平均 split candidate mark (ms) | 0.4320 | 0.1839 |
| split 单次 topology (us) | 1.666 | 2.326 |
| merge 单次 topology (us) | 0.829 | 1.459 |
| 最大 final leaf collect (ms) | 0 | 0 |
| 最大 topology issue | 0 | 0 |
| 最大 Q_s/active leaf 数量差 | - | 0 |

当前默认路径仍受 DOD 全量 Mesh emit 影响；这属于阶段 D，不作为 C1 的失败条件。

## 4. 20 万预算压力路径

输入为 Peking 513、Terrain size 80、Height scale 12、Max depth 20、split/merge `0.25/0.10 px`、200,000 triangle budget。有效帧排除 `timeSeconds == 0` 的初始化样本。原始数据为 [final1](c1-budget-saturation-20260817-final1.csv) 和 [final2](c1-budget-saturation-20260817-final2.csv)。

| 运行 | Classic split us | DOD split us | split 比值 | Classic merge us | DOD merge us | merge 比值 | DOD update ms |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| final1 | 2.435 | 2.663 | 1.094 | 1.418 | 1.517 | 1.070 | 223.817 |
| final2 | 2.366 | 2.662 | 1.125 | 1.365 | 1.478 | 1.083 | 225.091 |

两轮均保持 199999–200000 活动叶，所有帧 `passed=1`。B4 压力基线的 split 比值约为 `1.176`；C1 已达到降低 split heap 跳转的目标。merge 旁路刷新仍是 C2/O8 的对象。

## 5. 构建与测试

- OpenGL `RelWithDebInfo` 构建通过；
- D3D12 `RelWithDebInfo` 构建通过；
- OpenGL CTest：`11/11` 通过；
- OpenGL headless smoke：Classic、DOD 通过，GPU 在无上下文时按预期 skip；
- headless `budget-saturation`：Classic、DOD 两轮通过。
