# C2 阶段验收报告
## 1. 结论

C2/O8 已完成。DOD 保留连续的 `ActiveInternalNodes` / `ActiveLeafNodes`，将按 node 随机定位的活动和持久队列关系收拢到 `DataOrientedRoamNodeMembership` sidecar；这与 Classic 的 node-intrusive membership 语义一致，区别只在于 DOD 为 SoA node pool 使用外置紧凑布局。

本轮没有新增 Classic 没有的通用拓扑或 heap 规则，也没有改变 split/merge、canonical diamond、indexed heap 和 budget 的行为契约。

## 2. 实现变化

- 四组 position 从 `size_t` 压缩为 `uint32_t` sentinel；node index 本身也是 `uint32_t`，活动列表和 queue 都是 node pool 子集。
- 活动 internal、活动 leaf、Q_s position、Q_m position/representative/partner 由六组分散数组改为一个 24-byte/node sidecar。
- 热路径 active 判断只读取 sidecar sentinel；正向连续列表和反向位置的一致性检查仍由 validator 完成。
- `ActiveInternalNodes` 未删除，merge candidate mark 仍使用连续活动索引，避免扫描历史 node pool。

理论上，旧旁路元数据为四组 64 位 position 加两组 32 位 node metadata，共 40-byte/node；新 sidecar 为 6 个 32 位字段，共 24-byte/node。node pool 的 SoA 数值字段和 Classic 实现均未改变。

## 3. 回归证据

构建与测试：

- RelWithDebInfo build 通过；
- CTest `11/11` 通过；
- `roam_budget_reentry_classic`、`roam_budget_reentry_dod` 和 `roam_split_queue_view_dod` 通过。

压力 profile 使用 Peking 513、200,000 triangle budget、24 帧路径；有效帧排除初始化帧。原始数据见 [CSV](c2-budget-saturation-20260817.csv)。

| 指标 | Classic | DOD |
| --- | ---: | ---: |
| 有效帧数 | 23 | 23 |
| update median (ms) | 330.969 | 198.906 |
| update P95 (ms) | 343.899 | 217.052 |
| split topology median (ms) | 87.568 | 85.517 |
| merge topology median (ms) | 52.306 | 48.618 |
| active triangles | 200000 | 199999–200000 |
| max `Q_s - activeLeaf` | - | 0 |
| max topology issue | 0 | 0 |

这组压力数据用于确认 C2 没有增加拓扑事务、破坏 queue membership 或退化 merge candidate；它不是把整个 update 差异全部归因于 sidecar。C2 的直接收益来自旁路元数据容量和热路径活动判断，后续仍需结合 profiler 或多轮 A/B 单独归因。
