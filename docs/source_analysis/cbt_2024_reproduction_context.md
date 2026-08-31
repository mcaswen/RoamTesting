# CBT 2024 复现源码上下文

> 分析代码基线：提交 `078cb70603eb3a0a76dab14b81da9c12b3b3155d`，分析日期 2026-08-31。本文以当前主分支实际执行的 C++、HLSL 与 D3D12 命令记录为准；阶段 A-I 文档用于解释演进，不能反向替代当前源码。源码行号容易随提交变化，因此引用以文件和符号名为主。
>
> 配套阅读：[Classic CPU ROAM 源码上下文](classic_roam_context.md)给出对象式 CPU 基线；[Data-Oriented CPU ROAM 源码上下文](data_oriented_roam_context.md)给出 SoA 与并行 CPU 路径；[CBT 2024 接入计划](../parallel-roam/16-cbt-2024-integration-plan.md)记录阶段 A-I；[上游 large_cbt 架构参考](../parallel-roam/15-large-cbt-architecture-reference.md)描述参考工程；[官方语义基线 v1](../parallel-roam/18-cbt-2024-official-baseline-v1.md)冻结复现身份和容量矩阵；[最终实验数据分析与结论](../../benchmark-output/runtime-benchmark-final-analysis-20260828.md)提供本文采用的正式性能数据。
>
> 证据标签约定：
>
> - **源码事实：** 可以从当前 C++、HLSL、构建脚本或测试直接确认。
> - **根据实现推断：** 由控制流、资源布局或同步关系推导，仍应以 GPU capture 或 profiler 复核性能含义。
> - **经典算法背景：** 用于解释 CBT、LEB、rank/select 等背景，不表示项目复现了论文或上游工程的全部功能。
> - **尚无法确认：** 当前静态证据、自动测试或正式实验不足以支持结论。

## 1. 一页概览

**源码事实：** 当前 `CBT 2024` 是项目中唯一完全在 D3D12 GPU 上持续维护拓扑的 LOD 算法。它通过 `D3D12CbtTerrainLodAlgorithm` 接入统一 `ITerrainLodAlgorithm`，不生成 CPU mesh；普通帧由 GPU 完成分类、分裂规划、动态槽分配、四类 bisect 模板、双向邻接传播、simplify、三段 OCBT 归约、活动列表索引、增量高度图几何和 `ExecuteIndirect` 绘制。

一次稳定帧可以概括为：

```text
上一帧活动拓扑
  -> Reset 临时计数
  -> Classify 旧活动叶
  -> Split plan -> Allocate -> 邻接代复制 -> Bisect -> split propagation
  -> Prepare simplify -> Simplify -> merge propagation
  -> ReducePre -> ReduceFirst -> ReduceSecond
  -> Indexation / indirect args
  -> ModifiedOnly 或 FullDebug 几何
  -> 延迟诊断与 GPU timestamp
  -> ExecuteIndirect terrain render
```

| 结论 | 当前实现 |
| --- | --- |
| 公共接口 | `ITerrainLodAlgorithm` / `TerrainLodRenderPacket` |
| 算法身份 | `cbt_2024_official_baseline_v1`，显示名 `CBT 2024` |
| 图形后端 | 仅 D3D12；OpenGL UI 会标记为不可用 |
| GPU 要求 | Shader Model 6.6、64 位整数运算、64 位 typed-resource 原子操作 |
| 初始拓扑 | 方形高度图的 2 个根三角形、6 个有向半边 bisector |
| 动态容量 | 128K、256K、512K、1M 四档 OCBT |
| LOD 控制 | 投影三角形面积阈值，不是 CPU ROAM 的 nested-wedgie 像素误差 |
| 拓扑能力 | split、forced compatibility split、pair/quad merge、裂缝约束传播 |
| 几何能力 | 高度图采样、法线/UV/调试字段、modified-only 与 full-debug |
| 绘制能力 | GPU 活动索引 + 程序化顶点读取 + `ExecuteIndirect` |
| 诊断能力 | Off/Delayed/BlockingSmoke、故障锁存、整状态重建、18 阶段计时 |

**根据实现推断：** 这条路径的核心价值不是把 Classic 搬到 compute shader，而是让活动集合、空闲槽选择和绘制命令都留在 GPU。CPU 只提交常量、读取延迟统计并发布统一运行时数据，因此其性能结构与两个 CPU ROAM 路径根本不同。

## 2. “复现”的身份与证据边界

### 2.1 冻结身份

**源码事实：** [`Cbt2024Baseline.h`](../../src/algorithms/cbt_2024/Cbt2024Baseline.h) 的 `OfficialBaselineV1` 固定了以下身份：

| 字段 | 固定值 |
| --- | --- |
| `BaselineId` | `cbt-2024-official-baseline-v1` |
| `AlgorithmKey` | `cbt_2024_official_baseline_v1` |
| `DisplayName` | `CBT 2024` |
| `BenchmarkTag` | `benchmark/cbt-2024-official-baseline-v1` |
| 冻结基线提交 | `b0bdd5d0c25a523f5a15221dfb90eaf4db829b4c` |
| 官方上游 | `AnisB/large_cbt@7351e6fb380acc149b3aef22a6c39bf3df7950a6` |
| 本机兼容提交 | `7ae736d179528a0996449c0cc2db7f3279edc8ee` |

本机兼容提交只替换 NVIDIA 驱动不接受的 64 位 `firstbithigh` 写法，不改变 pass 顺序、拓扑状态或容量语义。项目冻结基线的提交与本文分析的当前 HEAD 也不是一个概念：前者保证正式实验身份不被覆盖，后者说明本文解释的是哪一版工程实现。

### 2.2 哪些部分来自上游语义

**源码事实：** 本项目保留了上游实现中决定 CBT 行为的骨架：

- 四档静态 OCBT 布局，以及压缩计数树加 64 位 bitfield；
- occupied/free rank-select 和显式 reduce；
- 面积分类、split pattern、空闲槽原子预留；
- bisect、simplify 与双向传播；
- LEB HeapID 路径解码；
- GPU 生成活动索引和间接参数。

参考入口包括上游的 [`ocbt_generic.hlsl`](../../third_party/large_cbt/shaders/shader_lib/ocbt_generic.hlsl)、[`bisector.hlsl`](../../third_party/large_cbt/shaders/shader_lib/bisector.hlsl)、[`leb.hlsl`](../../third_party/large_cbt/shaders/shader_lib/leb.hlsl) 和 [`mesh_updater.h`](../../third_party/large_cbt/demo/include/mesh/mesh_updater.h)。本项目没有在运行时直接链接或调用上游 demo，而是在自己的统一接口、D3D12 backend、地形资源和测试体系中实现等价协议。

### 2.3 不能把“复现”理解成什么

**源码事实：** 当前路径不是上游外太空 demo 的逐文件移植。两者至少存在这些有意差异：

- 上游以 `icosahedron.ccm` 的 20 面/60 半边球体为基础，本项目以方形高度图的 2 面/6 半边为基础；
- 上游包含行星、月球、水体、可见性缓冲和材质系统，本项目只接入统一 terrain LOD 与 D3D12 terrain renderer；
- 本项目增加共享 C++/HLSL ABI、CPU 精确参考、三种验证模式、延迟统计、故障恢复和统一 benchmark；
- 本项目默认绘制 active list，而不是把 visible list 当作唯一绘制集合；
- 本项目正式路径固定为 1280 × 720，并使用自己校准的 default/extreme 参数。

**经典算法背景：** “复现”在这里表示关键 CBT 拓扑与调度语义可追溯到冻结上游，而不是宣称场景、渲染器、资产或逐条指令完全相同。

## 3. 在统一 Terrain LOD 架构中的位置

**源码事实：** [`D3D12CbtTerrainLodAlgorithm`](../../src/algorithms/cbt_2024/d3d12/D3D12CbtTerrainLodAlgorithm.h) 实现 `ITerrainLodAlgorithm`，其能力声明为：

- 不支持 CPU mesh 输出；
- 支持 GPU-driven 与 procedural indirect rendering；
- 支持 split、merge、crack fix 和 topology validation；
- 每帧更新；
- 需要 SM 6.6、shader int64 与 64 位 typed-resource atomics。

[`D3D12TerrainRenderer::CreateAlgorithm`](../../src/render/D3D12TerrainRenderer.cpp) 负责按 `TerrainLodAlgorithmId::Cbt2024` 构造 adapter；[`QueryCbt2024Availability`](../../src/algorithms/cbt_2024/Cbt2024Support.cpp) 集中判断后端和硬件能力；[`ImGuiLayer`](../../src/gui/ImGuiLayer.cpp) 将能力状态映射到 `CBT 2024` 或 `CBT 2024（不可用）`。

### 3.1 输出契约

**源码事实：** `FillRenderPacket` 发布 `GpuProceduralIndirect` 数据包，借出四类原生资源：

| 数据包字段 | CBT 资源 | 消费者 |
| --- | --- | --- |
| `NativeVertexBuffer` | 最终 `TerrainMeshVertex` buffer | procedural terrain VS |
| `NativeActiveLeafBuffer` | 活动物理槽索引 | VS 由 `SV_VertexID / 3` 间接索引 |
| `NativeLodStateBuffer` | `CbtBisectorData` | LOD 调试着色 |
| `NativeIndirectDrawBuffer` | `CbtDrawState.Active` | `ExecuteIndirect` |

资源生命周期为 `UntilNextBuildOrReset`，`GpuResourceGeneration` 来自拓扑状态代次。`HasConsistentResourceContract()` 会检查 API、步长、容量、间接参数偏移和借用生命周期。

**源码事实：** 数据包里的 `ActiveTriangleCount` 是延迟诊断镜像，真正的绘制顶点数由 GPU 写入 `CbtDrawState.Active.VertexCountPerInstance`。因此诊断样本可能滞后一到数帧，但绘制不会依赖滞后的 CPU 数值。

## 4. 模块边界、所有权与依赖方向

### 4.1 运行时对象关系

```text
D3D12TerrainRenderer
  -> D3D12CbtTerrainLodAlgorithm           统一接口 adapter、恢复与统计映射
       -> D3D12CbtTerrainState
            -> D3D12CbtGpuState            持久拓扑/任务/索引/间接资源
            -> D3D12CbtFramePipeline       PSO、描述符、帧编排、状态镜像
                 -> D3D12CbtGeometryPipeline  高度图与分类/渲染几何
                 -> D3D12CbtDiagnostics       readback、timestamp、验证与故障锁存
```

**源码事实：** `D3D12CbtTerrainState` 先声明 `Topology`，后声明 `Pipeline`。C++ 成员逆序析构使 pipeline 先归还描述符，再释放 topology 资源。这不是偶然顺序，而是显式资源生命周期设计。

| 模块 | 拥有 | 不拥有 |
| --- | --- | --- |
| `D3D12CbtTerrainLodAlgorithm` | state、统一 stats、恢复次数/消息 | renderer、backend |
| `D3D12CbtGpuState` | 全部持久 topology `ID3D12Resource`、基础 payload、资源代次 | PSO、描述符 |
| `D3D12CbtFramePipeline` | root signatures、当前容量 PSO、常量缓冲、描述符区间、状态镜像 | topology 资源本体 |
| `D3D12CbtGeometryPipeline` | 高度纹理、upload、SRV、分类点、最终顶点、几何 PSO | active/modified 列表 |
| `D3D12CbtDiagnostics` | 每帧 readback/timestamp 槽、CPU 期望、快照、fault latch | GPU topology 状态 |

**根据实现推断：** 依赖方向是 adapter → 编排/资源/诊断，frame pipeline 通过只借用的 `D3D12CbtGpuResourceView` 访问 state。诊断不反向控制拓扑，故障恢复由 adapter 通过替换完整 state 完成，边界清晰。

### 4.2 CPU 参考层

`src/algorithms/cbt_2024` 根目录中的非 D3D12 文件承担可单测参考和协议定义：

| 文件 | 责任 |
| --- | --- |
| [`CbtGpuAbi.shared.h`](../../src/algorithms/cbt_2024/CbtGpuAbi.shared.h) | C++/HLSL 共用宏常量和字段偏移 |
| [`CbtGpuAbi.h`](../../src/algorithms/cbt_2024/CbtGpuAbi.h) | C++ 强类型常量与 flag 编解码 |
| [`CbtOccupancyTree.*`](../../src/algorithms/cbt_2024/CbtOccupancyTree.cpp) | CPU OCBT、reduce、occupied/free rank-select |
| [`CbtBisectorTopology.*`](../../src/algorithms/cbt_2024/CbtBisectorTopology.cpp) | 六基础半边、buffer 布局、HeapID、基础验证 |
| [`CbtClassification.*`](../../src/algorithms/cbt_2024/CbtClassification.cpp) | CPU 面积分类参考与真实高度图基础几何 |
| [`CbtSplitPlanner.*`](../../src/algorithms/cbt_2024/CbtSplitPlanner.cpp) | 保守预留、兼容链、pattern claim、空闲槽分配参考 |
| [`CbtBisectCommit.*`](../../src/algorithms/cbt_2024/CbtBisectCommit.cpp) | 四类 bisect 模板、邻接传播、LEB 子父关系参考 |
| [`CbtSimplifyCommit.*`](../../src/algorithms/cbt_2024/CbtSimplifyCommit.cpp) | pair/quad merge 与传播参考 |
| [`CbtTerrainGeometry.*`](../../src/algorithms/cbt_2024/CbtTerrainGeometry.cpp) | 位置、法线、UV、调试字段参考 |

这些实现不会替代生产 GPU pass；它们为第一次精确事务和单元测试提供同语义参照。

### 4.3 Shader 层

| Shader | 责任 |
| --- | --- |
| [`CbtGpuAbi.hlsli`](../../assets/shaders/dx12/cbt/CbtGpuAbi.hlsli) | 将共享宏暴露为 HLSL 常量/结构体 |
| [`CbtOccupancyTree.hlsli`](../../assets/shaders/dx12/cbt/CbtOccupancyTree.hlsli) | 容量特化压缩树、rank-select、三段 reduce |
| [`CbtTopologyE0.hlsl`](../../assets/shaders/dx12/cbt/CbtTopologyE0.hlsl) | 18 个生产 topology entry point |
| [`CbtBootstrap.hlsl`](../../assets/shaders/dx12/cbt/CbtBootstrap.hlsl) | indexation、indirect args、活动/修改几何、几何验证 |
| [`CbtProceduralTerrain.hlsl`](../../assets/shaders/dx12/CbtProceduralTerrain.hlsl) | 程序化 VS/PS、事件与深度调试着色 |

## 5. 初始化、重建、Reset 与故障恢复

### 5.1 首次初始化

**源码事实：** `BuildRenderData` 先检查 D3D12 device、帧是否已在 `BeginFrame` 与 `Present` 之间、硬件能力和高度图有效性。首次调用依次：

1. `D3D12CbtGpuState::Rebuild` 构造指定容量的基础 topology 与全部持久 buffer；
2. `D3D12CbtFramePipeline::Initialize` 创建 root signature、当前容量 PSO、描述符、常量缓冲、geometry 与 diagnostics；
3. `RecordFrame` 上传高度图并记录第一帧事务；
4. adapter 映射快照到 `TerrainLodStats` 和 render packet。

`D3D12CbtGpuState::Rebuild` 采用“先把新 topology 与资源全部构造成功，再替换旧 `Impl`”的提交方式。替换已有代次前会等待 GPU idle；失败不会留下半构造的公开资源视图。

### 5.2 哪些设置触发什么变化

| 变化 | 当前行为 |
| --- | --- |
| `CbtCapacity` | 等待 GPU idle，替换完整 terrain state，按新容量创建资源和 PSO |
| 高度图对象/算法切换 | renderer 的算法状态重建，重新上传高度图与基础几何 |
| `TerrainSize` / `HeightScale` | 保留 topology，但在分类前重算全部活动分类几何，并刷新渲染几何 |
| 面积阈值、最大深度、相机 | 只改变下一帧分类/提交，不重建资源 |
| `CbtGeometryMode` | 在 modified-only 与 full-debug 评估路径间切换 |
| validation mode | 改变完整验证/等待策略，不改变 topology 容量 |
| 显式 `Reset()` | 等待旧 GPU 工作，替换完整 state，统计和诊断代次归零 |

### 5.3 故障恢复

**源码事实：** `D3D12CbtDiagnostics::LatchFault` 保存首次故障，不允许后续样本覆盖根因。adapter 在 `RecordFrame` 失败或 pipeline 已 faulted 时等待 GPU idle，替换整个 `D3D12CbtTerrainState`，重新初始化并重试一次；恢复次数和最后故障会附加到状态消息。

**根据实现推断：** 这是代际重建而不是帧内事务回滚。它以更高的恢复成本换取资源、描述符、状态镜像、readback ring 与邻接 ping-pong 一起回到一致起点，避免局部修复遗漏隐含 GPU 状态。

## 6. 基础拓扑、物理槽与 HeapID

### 6.1 六个基础有向半边

**源码事实：** `BuildSquareCbtBaseTopology` 用方形地形的两个逆时针根三角形展开出 6 个有向 halfedge bisector。每个根三角形有 3 个方向版本：

- `Next` 与 `Previous` 在同一根三角形内形成三环；
- 共享对角线的两个 halfedge 互为 `Twin`；
- 开放边界的 `Twin` 为 `InvalidCbtBisectorIndex`。

18 个控制点按 6 × 3 保存。初始 active/visible list 均含 6 个基础槽，初始 indirect draw 顶点数是 18，modified position count 是 24。

### 6.2 物理槽与逻辑身份分离

**源码事实：** 动态 OCBT 只覆盖 `[0, N)`。六个基础 bisector 固定放在 `[N, N + 6)`，因此：

```text
BaseElementOffset = N
TotalElementCount = N + 6
```

物理槽决定数组地址，64 位 HeapID 决定 LEB 路径身份。基础 HeapID 是 `8..13`。当前共享 ABI 把 `CbtBaseDepth` 定义为 4，因为生产分类与 UI 统计采用 HeapID 的 bit length：`8` 到 `13` 都需要 4 位。

> 深度口径说明：冻结的官方语义基线差异表曾把方形基础深度写为 3，C++ 公共辅助函数 `CbtHeapIdDepth` 也返回零基的 `bit_width - 1`；生产 shader 的 `CbtHeapDepth`、`CbtBootstrapHeapDepth`、分类常量、诊断和 UI 则统一使用 `bit_width`，所以基础活动深度显示为 4。本文凡讨论运行时分类、最大活动深度和 `CbtBaseDepth` 均采用后一口径。

**经典算法背景：** LEB 路径身份让几何可以从根控制点和左右子路径重建，不需要为每个活动三角形持久保存三套完整顶点。物理槽可以在 merge 后复用，但 HeapID 决定它此刻代表哪一个逻辑三角形。

## 7. OCBT：压缩占用树与 rank-select

### 7.1 四档布局

**源码事实：** `BuildCbtOccupancyLayout` 只接受四个 2 的幂容量：

| 容量 | leaf depth | 最后一层计数深度 | bitfield `uint64` 数 | packed tree `uint32` 数 | 16384-bit subtree 数 |
| ---: | ---: | ---: | ---: | ---: | ---: |
| 128K | 17 | 10 | 2048 | 831 | 8 |
| 256K | 18 | 11 | 4096 | 1599 | 16 |
| 512K | 19 | 12 | 8192 | 3135 | 32 |
| 1M | 20 | 13 | 16384 | 6207 | 64 |

压缩树顶部 7 层用 32 位计数，中段用 16 位，最深计数层用 8 位；每个最深计数对应 128 个 occupancy bits。容量特化由 CMake 向 DXC 注入静态宏，不在 shader 内走四容量动态分支。

### 7.2 状态更新与显式归约

**源码事实：** set/clear 只改变 64 位 bitfield；压缩计数树不会随每次拓扑提交增量更新。split 与 merge 全部完成后，`CSReducePre`、`CSReduceFirst`、`CSReduceSecond` 从 bitfield 重建树：

1. `ReducePre` 汇总相邻两个 64 位 word，得到 128-bit block count；
2. `ReduceFirst` 在每个 16384-bit subtree 内向上归约；
3. `ReduceSecond` 完成顶部树与 root count。

这样 split 和 merge 可以共享一次最终 Reduce。

### 7.3 occupied 与 free rank-select

**源码事实：** `decode_bit(rank)` 逐层比较左子树活动数，最后在两个 64 位 word 中 select occupied bit。`decode_bit_complement(rank)` 不维护第二棵空闲树，而以“节点覆盖容量减活动计数”选择 free bit。

Allocate pass 针对每个被保守预留的 rank 调用旧树的 free decode。它只产生唯一物理槽映射，不提前设置 bit；真正写 HeapID、邻接和 occupancy bit 的提交点是 Bisect。

**根据实现推断：** 这种顺序让同帧多个候选看到相同旧 OCBT，却仍可用不重叠 rank 区间获得唯一槽。代价是 split 计划必须先保守预留，且新释放的 merge 槽要到下一帧才参与新的 free rank-select。

## 8. C++ / HLSL ABI 与 GPU buffer 协议

### 8.1 共享定义

**源码事实：** `CbtGpuAbi.shared.h` 是宏专用头，同时被 MSVC 和 DXC 消费；`CbtGpuAbi.h` 与 `CbtGpuAbi.hlsli` 分别提供语言适配。C++ 侧对关键结构执行 `sizeof` 与 `offsetof` 静态断言，从编译期约束字段漂移。

`CbtBisectorData` 是 8 个紧凑 `uint`，共 32 字节：

| word | 含义 |
| ---: | --- |
| 0 | subdivision pattern |
| 1..3 | 三个模板索引 |
| 4 | problematic neighbor |
| 5 | state：unchanged/bisect/simplify/merged |
| 6 | visible、modified、split/merge debug、事件寿命、活动深度 |
| 7 | propagation ID |

`CbtBisectorNeighbors` 是 `Previous/Next/Twin` 三个 `uint`，共 12 字节。

### 8.2 draw 与 dispatch 状态

**源码事实：** `CbtDrawState` 共 10 个 `uint`：

- `[0..3]`：active `D3D12_DRAW_ARGUMENTS`；
- `[4..7]`：visible `D3D12_DRAW_ARGUMENTS`；
- `[8]`：modified position count；
- `[9]`：显式 active bisector count。

geometry dispatch buffer 是连续三条 `D3D12_DISPATCH_ARGUMENTS`，共 9 个 `uint`，分别用于 active bisector、active positions 和 modified work。Topology 内部间接调度 scratch 同样是 9 个 word。

### 8.3 topology UAV 表

**源码事实：** topology root signature 绑定 `b0..b2`、`t0..t1` 和 `u0..u16`。主要 UAV 映射为：

| 寄存器 | 资源 |
| --- | --- |
| `u0/u1` | packed OCBT tree / 64-bit bitfield |
| `u2` | HeapID |
| `u3/u4` | current / next neighbors |
| `u5` | bisector data |
| `u6` | classification tasks |
| `u7` | simplification tasks |
| `u8` | allocation tasks |
| `u9` | propagation tasks |
| `u10` | memory counters |
| `u11` | topology indirect scratch |
| `u12` | draw state |
| `u13/u14/u15` | active / visible / modified indices |
| `u16` | validation counters |

`t0` 是分类用的 4 × `float3`/slot positions，`t1` 是上一帧 active indices。分类任务、simplify、allocation、propagation 和 validation buffer 的字数均由 `CbtTopologyBufferLayout` 与共享 ABI 计算，而不是在 renderer 中重复推导。

## 9. 完整逐帧事务与 18 个入口

### 9.1 18 个 topology entry point

**源码事实：** `OfficialBaselineV1::TopologyShaderEntryPoints` 与根 `CMakeLists.txt` 共同固定顺序；每个入口编译 128K/256K/512K/1M 四个容量变体：

| # | entry | 作用 |
| ---: | --- | --- |
| 1 | `CSResetE0` | 清临时计数、draw/dispatch 参数，保留旧 topology |
| 2 | `CSClassify` | 对上一帧活动叶分类 |
| 3 | `CSPrepareClassificationIndirect` | 把分类计数转为后续间接 dispatch |
| 4 | `CSSplitE2` | 兼容闭包、pattern claim、保守内存预留 |
| 5 | `CSPrepareAllocationIndirect` | 生成 allocation dispatch |
| 6 | `CSAllocateE2` | 从旧 OCBT free ranks 分配唯一动态槽 |
| 7 | `CSBisectE3` | 提交四类 split 模板和 occupancy bits |
| 8 | `CSPreparePropagationIndirectE3` | 生成 split propagation dispatch |
| 9 | `CSPropagateBisectE3` | 修复外部邻接引用 |
| 10 | `CSPrepareSimplifyF` | 检查合法 pair/quad merge 并准备任务 |
| 11 | `CSPrepareSimplifyIndirectF` | 生成 simplify dispatch |
| 12 | `CSSimplifyF` | 提交 merge、清 sibling bit、释放槽 |
| 13 | `CSPrepareSimplifyPropagationIndirectF` | 生成 merge propagation dispatch |
| 14 | `CSPropagateSimplifyF` | 修复或转发 merge 后外部邻接 |
| 15 | `CSReducePre` | 128-bit block 计数 |
| 16 | `CSReduceFirst` | subtree 内归约 |
| 17 | `CSReduceSecond` | 顶部归约与 root |
| 18 | `CSValidateF` | 全容量 topology 验证，按模式启用 |

`CSIndexation`、`CSPrepareIndirect`、几何 entry 和 `CSValidateGeometryG` 位于 `CbtBootstrap.hlsl`，不属于上述冻结的 18 个 topology entry。

### 9.2 `RecordFrame` 的真实命令顺序

**源码事实：** `D3D12CbtFramePipeline::RecordFrame` 的顺序比 entry 列表更完整：

1. 消费当前 frame slot 已完成的延迟诊断；`BlockingSmoke` 在非首帧先显式等待并消费全部；
2. 只在资源/管线代次的第一次验证事务建立 CPU 精确期望；
3. 写三组每帧常量，绑定 bootstrap/topology descriptors；
4. 首帧或 `TerrainSize/HeightScale` 改变时，先全量重建分类几何；
5. Reset、Classify、PrepareClassificationIndirect；
6. Split、PrepareAllocationIndirect、Allocate；
7. 用 `CopyResource` 把 current neighbor generation 复制到 next generation；
8. Bisect、PreparePropagationIndirect、PropagateBisect；
9. PrepareSimplify、PrepareSimplifyIndirect、Simplify；
10. PrepareSimplifyPropagationIndirect、PropagateSimplify；
11. 三段 Reduce；
12. 把候选、内存、提交计数和 OCBT root 先复制到本帧延迟诊断槽；
13. `CSIndexation` 全槽扫描，`CSPrepareIndirect` 生成 draw/geometry dispatch；
14. modified-only 或 full-debug 渲染几何；
15. 完整验证模式先验证 compact active geometry，再执行 full-capacity topology validation，并以最终 validation 头覆盖诊断槽；Off 只追加最大活动深度；
16. 复制最终 draw state 和精确参考所需的小范围数据，resolve timestamps，并把资源恢复到 SRV/INDIRECT_ARGUMENT 状态；
17. 成功后切换邻接 read generation，递增 topology frame generation。

**源码事实：** `Reset` 只清瞬态任务、计数和本帧输出，不清上一帧 active count、HeapID 或 occupancy；否则 `Classify` 就没有上一帧活动叶可读。

## 10. Classify：面积阈值而非 CPU ROAM 误差

**源码事实：** 分类输入不是最终 3 顶点 render buffer，而是每槽 4 个 `float3`：当前三角形的 3 个顶点加父级/辅助位置。分类依次处理：

- 背面状态；
- 三角形 AABB 与左右上下四个视锥平面；近远边界继续由投影和背面条件处理；
- 投影到 drawable 后的像素面积；
- grazing angle 下的保守放大；
- `CbtTriangleAreaPixels`、父级面积与 `MaxDepth`。

分类结果包含 back-face culled、frustum culled、too small、unchanged、bisect。低于面积阈值约一半并满足父级条件的活动叶成为 simplify 候选；高于阈值且未到最大深度的叶成为 split 候选。

**源码事实：** Classic 与 DOD 共用 nested-wedgie thickness、屏幕空间误差和 split/merge 双阈值；CBT 不共用这个质量函数。统一设置结构中因此同时存在 CPU 的 `ScreenSpaceSplitThresholdPixels/ScreenSpaceMergeThresholdPixels/TriangleBudget` 和 CBT 的 `CbtTriangleAreaPixels/CbtCapacity`。

**根据实现推断：** default/extreme 中三算法三角形规模接近，是基准参数校准的结果，不是三个算法优化了同一个误差函数。CBT 面积阈值提供近似工作量控制，不能解释成硬三角形预算。

## 11. Split 计划与空闲槽分配

### 11.1 兼容闭包

**源码事实：** 一个被分类为 bisect 的叶可能要求 facing neighbor 或更深的兼容链同时细分。`CSSplitE2` 沿邻接和深度关系传播，原子 OR 到每个参与节点的 subdivision pattern，并记录：

- 重复 split claim；
- 多条链共享的 compatibility node；
- 总兼容链步数；
- 最长兼容链长度。

候选用原子 append 收集，没有按面积收益做全局排序。

### 11.2 保守内存预留

对当前深度 `d` 和基础深度 `d0`，内部候选采用：

```text
maximumRequiredMemory = 2 * (d - d0) - 1
```

边界候选只需 1 个槽，直接 facing twin 情形可按 2 个槽处理。线程先原子预留上界，发现不能完整满足时恢复计数并拒绝整条候选，避免提交一半兼容闭包。

**根据实现推断：** 预留上界可能大于四类模板最后实际消耗，因此它保证安全但不保证在高压力下最大化视觉收益。多个闭包共享节点会进一步产生保守空洞，这正是后续“闭包感知预算调度”研究可替换的决策层，而不应修改冻结 baseline 身份。

### 11.3 allocation

`CSAllocateE2` 将每个获准候选的连续 free-rank 区间解码为实际物理槽，写入 allocation task buffer。分配阶段不改邻接、HeapID 或 occupancy；因此它可以与下一步模板提交保持清晰事务边界。

## 12. Bisect 四模板与 split 传播

### 12.1 四类提交模板

**源码事实：** subdivision pattern 的 center/right/left 三个位组合成四类生产模板：

| 模板 | pattern | 新动态槽 | 结果活动叶 |
| --- | --- | ---: | ---: |
| center | center | 1 | 2 |
| right double | center + right | 2 | 3 |
| left double | center + left | 2 | 3 |
| triple | center + right + left | 3 | 4 |

`CSBisectE3` 读取 allocation，写子 HeapID、current/next neighbor、`CbtBisectorData`、debug flags、occupancy bit 和 propagation task。CPU 的 `CommitCbtBisects` 与 `EvaluateCbtLebTriangle` 是同语义参考。

### 12.2 邻接 ping-pong

**源码事实：** 每帧提交前，frame pipeline 把 current neighbor buffer 用 D3D12 `CopyResource` 复制到 next buffer。split 与 simplify 都写 next generation，帧成功后只切换一次 read index。

这样未修改槽自动保留旧邻接，修改槽在 next 上形成新状态。只要整帧事务没有成功到达末尾，公开 read generation 就不会切换。

### 12.3 split propagation

局部模板只能直接修复闭包内部邻接；闭包外部仍可能指向已被替换的父槽。`CSPropagateBisectE3` 根据 propagation task 原子/条件替换外部 `Previous/Next/Twin` 分量，并统计 rewrite 数。

**经典算法背景：** 这承担了 CPU ROAM forced split 保持无 T-junction 的同类目标，但实现机制不同：CPU 在指针/索引拓扑上递归提交，CBT 先形成模式与任务，再在 GPU 批量传播。

## 13. Simplify、pair/quad merge 与反向传播

**源码事实：** simplify 不是“看到两个小三角形就清 bit”。`CSPrepareSimplifyF` 会检查：

- sibling/parent HeapID 关系；
- facing pair 或四节点 diamond 的深度一致性；
- 参与节点都处于可合并状态；
- 由最小 HeapID 等规则形成唯一 canonical submission，防止重复提交。

`CSSimplifyF` 将保留槽的 HeapID 上移到 parent，清除被释放 sibling 的 HeapID 和 occupancy bit，更新状态/事件标记，并生成外部传播任务。合法二节点 merge 释放 1 个动态槽，四节点 merge 释放 2 个动态槽。

`CSPropagateSimplifyF` 把外部邻接从已释放子槽修复到保留 parent，必要时转发邻接引用。CPU `CommitCbtSimplifications` 覆盖相同的 pair/quad 参考情形。

**源码事实：** 同帧顺序是 split 在前、simplify 在后，二者写同一个 next neighbor generation，最后共享一次 Reduce。这不等于 CPU DOD 的“预算不足先 merge 再 split”交叉队列；CBT 候选来自面积分类和 GPU 原子提交。

## 14. Reduce、Indexation 与间接绘制命令

### 14.1 为什么提交后必须 Reduce

Bisect 和 Simplify 改了 occupancy bitfield，却没有同步更新压缩计数树。三段 Reduce 结束后，root 才重新等于活动动态槽数，下一帧 free/occupied rank-select 才有有效计数。

### 14.2 Indexation 是全物理容量扫描

**源码事实：** `CSIndexation` 扫描 `TotalElementCount`，以 `HeapID != 0` 判断槽是否活动：

- append 到 active indices；
- visible flag 为真时 append 到 visible indices；
- 只有同时 visible 且 modified 时才 append 到 modified indices，因此 `modified ⊆ visible ⊆ active`；
- 从 HeapID bit length 归约最大活动深度。

随后 `CSPrepareIndirect` 写 active/visible draw args 和三条 geometry dispatch args。生产 baseline 的 terrain draw 使用 active draw state，因此被视锥剔除的叶仍可保留在 topology 中，而不是因本帧不可见就销毁。

**根据实现推断：** Indexation 的成本随容量 `N + 6` 增长，而不是只随活动数增长。正式容量矩阵中 default 的 GPU 阶段合计从 128K 的 0.0834 ms 增长到 1M 的 0.0938 ms，符合容量扫描存在固定增量的表现，但各 pass 占比仍需 GPU profiler 才能精确归因。

## 15. 高度图几何与程序化渲染

### 15.1 高度图与分类几何

**源码事实：** `D3D12CbtGeometryPipeline` 上传 `R32_FLOAT` 高度纹理并创建 SRV。Shader 用 `Texture2D::Load` 和手工双线性插值采样高度；法线由四点有限差分得到。

分类位置 buffer 与最终渲染顶点 buffer 分离：

- 分类位置：每槽 4 × `float3`，三个 child 点位于 `3 * slot`，parent 辅助点位于独立的 `3 * TotalElementCount + slot` 平面；
- 最终渲染顶点：每槽 3 × 52-byte `TerrainMeshVertex`；
- `static_assert(sizeof(TerrainMeshVertex) == 52)` 约束 C++/shader stride。

HeapID 通过基础控制点和 LEB 左/右子路径恢复 UV，再映射到 `TerrainSize`、高度与法线；不需要 CPU 展开活动 mesh。

### 15.2 modified-only 与 full-debug

**源码事实：** 默认 `ModifiedOnly` 只对 modified index list 对应槽更新最终 3 顶点。`FullDebug` 每帧重算全部 active 槽，用于验证增量结果，不是生产性能路径。首帧、资源重建或几何尺度失效时会执行必要的全量活动几何更新。

**源码事实：** 当前没有“根据 modified/active 比例自动切换全量几何”的启发式。模式由设置显式选择，尺度失效由正确性条件强制全量。

### 15.3 renderer 数据流

程序化 VS 的索引链为：

```text
SV_VertexID
  -> activeOrdinal = SV_VertexID / 3
  -> localVertex = SV_VertexID % 3
  -> physicalSlot = ActiveIndices[activeOrdinal]
  -> RenderVertices[physicalSlot * 3 + localVertex]
  -> CbtBisectorData[physicalSlot] 提供 LOD 调试字段
```

draw args 由 GPU 生成并由 `D3D12ProceduralTerrainPipeline` 通过 `ExecuteIndirect` 消费。split 事件显示红色，merge 事件显示绿色；事件寿命为 16 帧，深度也编码在 `Flags` 中。这些颜色是诊断可视化，不参与拓扑决策。

## 16. D3D12 状态、同步和资源代次

**源码事实：** frame pipeline 和 geometry pipeline 分别维护自己拥有/借用资源的状态镜像，在 UAV、non-pixel SRV、copy source/destination 和 indirect argument 之间插入显式 barrier。邻接复制已使用真正的 COPY_SOURCE/COPY_DEST `CopyResource`，不是按容量 compute copy。

普通帧不会为 topology 结果执行 CPU fence wait。诊断 readback 以 swapchain frame slot 轮转；slot 再次被 `BeginFrame` 复用时，其旧 GPU 工作已经由 backend fence 保证完成，然后 CPU 才消费映射数据。

显式 GPU idle 只出现在需要改变所有权边界的路径：

- 容量切换；
- 显式 `Reset()`；
- fault 后完整 state 重建；
- `BlockingSmoke` 验证模式要求立即确认当前事务。

**根据实现推断：** 资源 generation 和 topology frame generation 分工不同。前者标识资源地址是否失效，后者标识同一资源代内已成功提交多少帧事务；诊断 sample generation 再标识 CPU 当前看到哪一帧。

## 17. 诊断、验证、统计年龄与恢复

### 17.1 三种验证模式

| 模式 | GPU 工作 | CPU 等待 | 用途 |
| --- | --- | --- | --- |
| `Off` | 轻量计数/root/draw/depth readback + timestamps | 普通帧无主动等待 | UI 与正式 benchmark |
| `Delayed` | 轻量诊断 + full-capacity topology/geometry validation | 随 frame ring 延迟消费 | 持续正确性检查 |
| `BlockingSmoke` | 与 Delayed 相同 | 非首帧显式 idle 并立即消费 | 自动 smoke 与故障定位 |

**源码事实：** Off 并非“零 readback”。`DiagnosticReadbackBytes` 为 140；CBT diagnostics 为前 17 个阶段读取 272 字节 timestamp，renderer 为 `TerrainRender` 读取另一个 16 字节 timestamp，因此统一统计发布 428 bytes/frame。完整 validation 把 140 字节诊断段替换为 1556 字节，再加这两部分 timestamps。

### 17.2 诊断检查内容

轻量快照包含候选、预留、提交、传播、merge、释放、模板计数、OCBT root、draw state、活动数和最大深度。完整验证再检查：

- occupancy bit 与 HeapID 是否一致；
- active index 和 draw count 是否一致；
- neighbor 是否越界、是否指向活动槽、互反关系是否成立；
- allocation/memory 守恒；
- 基础六半边及分类/渲染几何是否匹配 CPU 期望；
- compact active geometry 是否满足位置、法线、UV 与 stride 协议。

第一笔验证事务会运行 CPU 的 classification、split planner、allocation、bisect/simplify 与基础 geometry 精确参考。后续帧主要依赖 GPU validator 和守恒条件，避免每帧在 CPU 上复演全容量拓扑。

### 17.3 延迟样本语义

`GpuTopologyFrameGeneration` 是刚记录成功的 GPU 事务代次，`GpuClassificationSampleGeneration` 和 `CbtGpuTimingSampleGeneration` 是最近已完成 readback 的代次；二者差值发布为 sample age。重复代次、零代次或跳代会设置 dropped 标记。

**源码事实：** UI 和 CSV 展示的活动数/阶段时间属于该延迟样本；renderer 的当前帧 draw 仍直接消费 GPU indirect state。比较性能时必须按 sample generation 对齐，不能把 CPU 展示年龄误当成 GPU 拓扑停滞。

## 18. 运行时 UI、统一统计与 benchmark 链路

**源码事实：** `Application`、`D3D12TerrainRenderer`、`TerrainLodStats`、`TerrainRenderStats`、`ImGuiLayer` 和 `RuntimeBenchmark` 构成发布链：

```text
D3D12CbtDiagnostics snapshot
  -> D3D12CbtTerrainLodAlgorithm::Stats
  -> D3D12TerrainRenderer::Stats
  -> Application debug/benchmark state
  -> ImGui 指标与 Markdown/CSV
```

UI 中 CBT 能显示容量、面积阈值、validation/geometry mode、活动/剩余动态槽、split/simplify 候选、提交/释放、pair/quad、传播、样本年龄、readback 和 18 阶段 GPU 时间。

18 个 timestamp 展示阶段为：

1. Classification geometry
2. Reset
3. Classify
4. Split
5. Allocate
6. Neighbor copy
7. Bisect
8. Propagate bisect
9. Prepare simplify
10. Simplify
11. Propagate simplify
12. Reduce pre
13. Reduce first
14. Reduce second
15. Indexation / indirect
16. Render vertex evaluation
17. Validation
18. Terrain render

前 17 个 query 由 `D3D12CbtDiagnostics` 在 compute/geometry pipeline 周围记录；最后一项由 renderer 的 graphics pipeline 单独补齐，并用 topology generation 对齐。

统一逻辑阶段映射为：

```text
Split ms = Split + Allocate + NeighborCopy + Bisect + PropagateBisect
Merge ms = PrepareSimplify + Simplify + PropagateSimplify
Emit ms  = ClassificationGeometry + RenderGeometry
Validate = Validation
```

这让总报告可以把 CBT 放进 Classic/DOD 的逻辑列，但不能把 GPU timestamp 与 CPU wall-clock 子阶段解释成同一种计时机制。

## 19. 默认与极限路径的冻结参数

**源码事实：** `OfficialBaselineV1` 固定 1280 × 720 和以下输入：

| 参数 | 默认路径 | 极限路径 |
| --- | ---: | ---: |
| 高度图 | Test129，129 × 129 | Peking，解码 547 × 547 |
| `TerrainSize` | 30 | 80 |
| `HeightScale` | 4 | 12 |
| `MaxDepth` | 20 | 20 |
| CPU split/merge px | 4 / 2 | 0.25 / 0.10 |
| CPU triangle budget | 20,000 | 200,000 |
| CBT capacity | 128K | 1M |
| CBT area px² | 58 | 2.05 |
| samples / warmup | 600 / 16 | 64 / 24 |

容量矩阵实验执行 `default/extreme × 4 capacities × 3 repeats`，validation 为 Off、geometry 为 ModifiedOnly、VSync 为 Off。阶段 I 之后若改变身份、入口顺序或正式参数，必须建立新算法键/基线版本，而不是覆盖 v1。

## 20. 正式性能结果

以下数字直接摘自最终实验报告，不在本文重新生成。

### 20.1 总帧时间与尾延迟

| 路径 | 算法 | 平均 ms | P95 ms | P99 ms | 平均值下 CBT 相对该算法 |
| --- | --- | ---: | ---: | ---: | ---: |
| 默认 | Classic | 4.720 | 6.639 | 8.634 | CBT `-93.24%` |
| 默认 | DOD | 3.486 | 5.112 | 6.250 | CBT `-90.85%` |
| 默认 | CBT | 0.319 | 0.489 | 0.642 | 基准 |
| 极限 | Classic | 201.846 | 225.289 | 234.723 | CBT `-99.53%` |
| 极限 | DOD | 119.017 | 141.885 | 158.894 | CBT `-99.21%` |
| 极限 | CBT | 0.939 | 2.012 | 16.574 | 基准 |

**源码事实：** 极限 CBT 的 pooled P95/P99 被第一轮异常抬高；后四轮的 GPU 阶段合计稳定在约 0.173 ms，而第一轮存在明显异常。正式结论保留该样本，不把异常静默删除。

### 20.2 三角形工作量可比性

| 路径 | CBT 平均三角形 | CPU ROAM 平均 | 相对目标 |
| --- | ---: | ---: | ---: |
| 默认 | 18,732.14 | 18,758.02 | `-0.138%` |
| 极限 | 201,811.17 | 200,000 | `+0.906%` |

极限 CBT 逐帧约在 183K–217K 间变化，因为 2.05 px² 是面积阈值，不是 200K 硬上限。工作量接近足以支持工程对比，但不能证明质量函数严格等价。

### 20.3 CBT 逻辑阶段

| 路径 | Split ms | Merge ms | Emit ms |
| --- | ---: | ---: | ---: |
| 默认 | 0.0267 | 0.0136 | 0.0053 |
| 极限 | 0.0760 | 0.0260 | 0.0233 |

最终报告中的 GPU 细分显示，极限路径 `TerrainRender` 约 0.12848 ms，占 pooled GPU 阶段合计约 38.5%；`Classify` 约 0.03973 ms，随后是 render geometry、allocate、indexation 和 neighbor copy。默认路径各 compute pass 更接近固定启动成本。

### 20.4 容量矩阵解释

官方基线容量实验显示：

- 默认路径四容量都得到 18,732.1417 平均三角形；GPU stage sum 从 0.0834 ms 缓慢升至 0.0938 ms；
- 极限 128K 只有 120,771.5573 平均三角形，最低仅余 355 槽，三次 repeat 有 54.5173 的均值标准差；
- 极限 256K/512K/1M 都得到 201,811.1719，说明 256K 起该输入未受容量截断；
- 正式极限仍选 1M，为后续相机轨迹和诊断留出容量余量。

**根据实现推断：** 128K 极限结果反映池饱和和原子提交顺序，而不是面积分类失效。容量是可驻留动态 bisector 上限，不是活动三角形硬预算。

## 21. Classic、DOD 与 CBT 对照

| 维度 | Classic CPU | Data-Oriented CPU | CBT 2024 GPU |
| --- | --- | --- | --- |
| 拓扑表示 | 对象节点、裸指针 bintree | SoA 节点池、稳定索引 | 固定槽数组、HeapID、邻接 ping-pong |
| 活动集合 | 持久叶对象/队列 membership | 稠密 active leaves + sidecar | 每帧 full-capacity indexation |
| 空闲资源 | CPU 节点分配 | 节点池 free/history child | OCBT free rank-select |
| 质量模型 | nested wedgie 屏幕误差 | 与 Classic 相同 | 投影三角形面积 |
| 调度 | 持久 `Q_s/Q_m` crossover | 持久队列 + chunk 条件并行 | 无序原子候选 + 保守闭包预留 |
| 裂缝处理 | forced split / diamond | 同 Classic | split pattern + 双向 propagation |
| merge | diamond merge | pair/diamond + 队列收敛 | canonical pair/quad simplify |
| 几何 | 持久增量 CPU indexed mesh | 持久增量 CPU indexed mesh | GPU modified slots 顶点评估 |
| 上传/读回 | CPU mesh 增量上传 | CPU mesh 增量上传 | 无 CPU mesh 上传；Off 为 428 B readback |
| 绘制数量 | CPU index count | CPU index count | GPU draw state + `ExecuteIndirect` |
| 预算含义 | active leaf 硬上限 | active leaf 硬上限 | 动态池容量 + 面积阈值 |
| 并行边界 | 单线程 | CPU 评分/条件拓扑并行 | GPU pass、原子任务与间接 dispatch |

**源码事实：** CBT 没有沿用 Classic/DOD 的 persistent priority queues，也没有实现“先 merge 回收预算，再按收益 split”的同一控制环。它改变的不只是数据布局，还改变了 LOD 质量模型、预算语义和执行设备。

**根据实现推断：** 因此 CBT 的数量和速度可以做统一场景下的工程比较，但不能仅凭总帧时间断言三者产生同等误差分布。严格质量比较还需要屏幕空间误差图、最大误差和 P95/P99 误差等独立指标。

## 22. 逐帧手算示例

下面用抽象槽说明事务边界，不代替真实 neighbor 模板。

初始状态：动态容量记为 8，只展示动态槽；六个基础槽位于 `8..13`。基础槽 `8` 的 HeapID 为 8，准备执行 center split；动态槽 0/1 是另一棵基础三角形下的一对活动 sibling，准备执行 pair simplify。

```text
旧 OCBT bits: 00000011
旧 root count: 2
旧 active: [0,1,8,9,10,12,13]
```

1. `Classify` 追加一个 split candidate 和一个 simplify candidate。
2. `Split` 为 center pattern 保守预留 1 槽。
3. `Allocate` 对旧 OCBT 调用 `decode_bit_complement(0)`，得到动态槽 2，但暂不置位。
4. 邻接 current 完整复制到 next。
5. `Bisect` 用基础槽 8 与动态槽 2 表示两个子 bisector，写子 HeapID、邻接、modified/split flag，并设置 bit 2。
6. `PropagateBisect` 把闭包外指向旧父关系的邻接改到子关系。
7. `PrepareSimplify` 重新验证槽 11 所在 sibling/diamond；若 split 已使其条件失效，则不提交；若仍合法，则 canonical 线程生成任务。
8. `Simplify` 上移保留 HeapID、清被释放动态 sibling bit，并设置 merge flag。
9. 三段 Reduce 从最终 bits 重建 root；不是从第 3 步的旧树推导。
10. `Indexation` 扫描 14 个物理槽，生成新的 active/visible/modified lists 和 draw args。
11. `ModifiedOnly` 只重算 split/merge 涉及的槽，renderer 立即用新的 GPU draw args 绘制。
12. CPU 在后续 frame slot 完成时才看到这帧统计；绘制本身不等待 readback。

这个例子强调三个不变量：allocation 只分配地址，Bisect 才提交 occupancy；split 与 merge 共享 next 邻接代；Reduce 和 Indexation 只看整帧最终状态。

## 23. 关键不变量与验证覆盖

### 23.1 OCBT 不变量

- bit 为 1 当且仅当对应动态槽有活动 HeapID；
- Reduce 后 root 等于活动动态槽数；
- occupied/free ranks 均解码到唯一且范围内的物理槽；
- `activeDynamic + remainingDynamic = capacity`；
- 基础六槽不进入 OCBT bitfield。

### 23.2 topology 不变量

- 活动槽 HeapID 非零，非活动动态槽 HeapID 为零；
- `Previous/Next/Twin` 为 invalid 或范围内活动槽；
- twin 和环邻接满足预期互反关系；
- split 闭包不能部分提交；
- pair/quad merge 只能由唯一 canonical candidate 提交；
- 成功帧才切换邻接 read generation。

### 23.3 draw/geometry 不变量

- active draw vertex count 等于 `3 × activeBisectorCount`；
- active indices 只含活动槽且不重复；
- modified list 只含本帧需要重评估且可见的槽，并满足 `modified ⊆ visible ⊆ active`；
- 每物理槽有 3 个 52-byte render vertex 和 4 个 classification position；
- resource generation 与 render packet 借用地址共同变化。

### 23.4 自动测试

CPU 参考测试覆盖：

- `cbt_occupancy_tree`
- `cbt_bisector_topology`
- `cbt_classification`
- `cbt_split_planner`
- `cbt_bisect_commit`
- `cbt_simplify_commit`
- `cbt_terrain_geometry`

D3D12 回归重点包括：

- `cbt_procedural_h_quick` 及 256K/512K/1M 容量变体；
- `cbt_runtime_h_default_quick`、`cbt_runtime_h_extreme_quick`；
- `cbt_runtime_h_full_geometry_quick`；
- `cbt_runtime_i_baseline_quick`。

`D3D12CbtOccupancyTree` 另有 capacity-specialized GPU OCBT smoke，覆盖位更新、三段 reduce 和 rank-select 的 CPU/GPU 对照。

## 24. 符号与文件索引

| 要追踪的问题 | 首选文件/符号 |
| --- | --- |
| 算法身份与正式参数 | `Cbt2024Baseline.h` / `OfficialBaselineV1` |
| 运行时可用性 | `Cbt2024Support.cpp` / `QueryCbt2024Availability` |
| 统一接口和恢复 | `D3D12CbtTerrainLodAlgorithm.cpp` / `BuildRenderData` |
| GPU 资源布局与代次 | `D3D12CbtGpuState.cpp` / `Rebuild`、`Resources` |
| 帧 pass 顺序 | `D3D12CbtFramePipeline.cpp` / `RecordFrame` |
| root signature 与 PSO | `D3D12CbtFramePipeline.cpp` / `CreateTopologyRootSignature`、`CreatePipelines` |
| 高度图与顶点资源 | `D3D12CbtGeometryPipeline.cpp` / `Initialize` |
| 延迟 readback | `D3D12CbtDiagnostics.cpp` / `QueueSample`、`ConsumeCompleted` |
| GPU timestamp | `D3D12CbtDiagnostics.cpp` / `BeginGpuStage`、`EndGpuStage`、`ResolveGpuTimings` |
| OCBT CPU 参考 | `CbtOccupancyTree.cpp` / `Reduce`、`DecodeBitComplement` |
| 基础六半边 | `CbtBisectorTopology.cpp` / `BuildSquareCbtBaseTopology` |
| C++/HLSL 字段协议 | `CbtGpuAbi.shared.h`、`CbtGpuAbi.hlsli` |
| topology shader | `CbtTopologyE0.hlsl` / 18 个冻结 entry |
| index/geometry shader | `CbtBootstrap.hlsl` / `CSIndexation`、`CSPrepareIndirect` |
| terrain draw shader | `CbtProceduralTerrain.hlsl` |
| renderer indirect draw | `D3D12ProceduralTerrainPipeline.cpp` / `ExecuteIndirect` 路径 |
| UI 选择与指标 | `ImGuiLayer.cpp` / algorithm combo、CBT metrics |
| benchmark 序列与导出 | `Application.cpp`、`RuntimeBenchmark.cpp` |

## 25. 已知限制与待 profiling 项

### 25.1 已知限制

**源码事实：** 当前实现仍有以下明确边界：

- 只支持 D3D12；OpenGL 后端不能运行 CBT；
- 只提供四个静态容量，容量切换需要完整资源/PSO 代际重建；
- split candidates 无全局收益排序，池压力下提交结果可能受 GPU 原子调度影响；
- 保守兼容闭包预留可能留下未用容量；
- 面积阈值不是硬 triangle budget，也不是 Classic/DOD 的 wedgie 误差；
- Indexation 与完整 validation 按总容量扫描；
- modified-only 没有按脏槽比例自动切换全量几何；
- Delayed 模式只能在若干帧后发现故障，恢复是完整 state 重建；
- baseline 绘制 active list，visible list 主要保留给诊断/后续变体；
- 当前没有跨 vendor、跨 GPU 型号的正式可移植性能结论。

### 25.2 待 profiler 回答

**尚无法确认：** 仅凭 timestamp 和源码还不能回答：

- 1M 极限路径上 Classify、Indexation 与 TerrainRender 的具体 memory/cache bottleneck；
- 不同显卡上 64 位 atomics 对 Split/Allocate 的吞吐影响；
- modified ratio 达到多少时 full geometry 更便宜；
- active 与 visible 分离后是否值得只绘制 visible list，以及对边界稳定性的影响；
- 更细粒度的 OCBT reduce 或活动槽 compaction 是否能抵消额外 pass；
- 闭包收益排序能否在相同容量下改善误差而不破坏 baseline 的吞吐优势。

这些问题需要 PIX/厂商 profiler、跨设备重复实验或新的算法键验证，不能从当前正式报告外推。

## 26. 许可证与发布门禁

**源码事实：** 截至冻结文档记录的 2026-08-25，上游仓库没有可识别的 `LICENSE`、`COPYING`、SPDX 声明或 README 许可证条款。公开可读源码不等于获得复制、修改或再分发许可。

因此当前复现可以用于本地技术研究、基线对照和架构分析，但在作者提供兼容许可证或书面授权前，不能把上游衍生实现当作已获准公开发布的组成部分。该限制是发布门禁，不是否定本地测试已经完成。

## 27. 文档一致性与架构审查

本次审查按当前 HEAD 对照 adapter、GPU state、frame pipeline、geometry、diagnostics、共享 ABI、topology/bootstrap/render shaders、UI、renderer、benchmark 与测试入口，结论如下：

- 文档中的模块所有权与当前拆分一致，没有把 PSO/描述符责任重新归到 GPU resource state；
- 逐帧顺序以 `D3D12CbtFramePipeline::RecordFrame` 为准，阶段 A-I 仅作历史索引；
- 18 个冻结 topology entry 与 `Cbt2024Baseline.h`、CMake 列表一致；18 个 GPU 计时阶段则包含 renderer 的 `TerrainRender`，两种“18”已明确区分；
- 基础深度按当前 HeapID bit-length 语义记录为 4，并显式标出冻结差异表中“3”的历史口径；
- CPU 与 HLSL ABI、32-byte bisector data、10-word draw state、9-word geometry dispatch 和 19-word validation state 已按共享头核对；
- 性能数字只引用最终实验报告和官方容量基线，没有把新产生但未冻结的临时报告混入结论；
- Classic、DOD 与 CBT 的质量模型和预算语义已分开，没有因三角形数量接近而宣称算法等价；
- 本文只增加维护者文档与导航，不改变运行时代码、shader、公开接口或历史 DOCX。

从当前架构看，资源所有权、编排、几何、诊断和 adapter 已有可维护边界；后续研究变体应优先包装或扩展这些边界，并使用新的算法身份，而不应继续修改冻结 baseline 的语义。
