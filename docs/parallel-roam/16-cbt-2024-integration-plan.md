# CBT 2024 接入与复现计划

> 初稿日期：2026-07-16
> 源码复核：2026-08-23
> 状态：实施计划 v0.8；阶段 E3 已完成，当前进入阶段 F
> 前置条件：D3D12 迁移阶段已完成
> 上游参考：`third_party/large_cbt`，提交 `7ae736d179528a0996449c0cc2db7f3279edc8ee`
> 本机兼容基线：提交 `7ae736d179528a0996449c0cc2db7f3279edc8ee`，仅替换 NVIDIA 64 位 `firstbithigh` 实现

> 主分支已移除 GPU ROAM-like 过渡实现。它只作为历史实验保留在 `archive/gpu-roam-like` 分支，不再属于当前接入架构或完成后的主线比较集合。

## 1. 目标与完成边界

本计划的目标是在 RoamTesting 现有 D3D12 管线中接入 CBT 2024 的完整 GPU 常驻拓扑，并在不改变上游算法语义的前提下适配高度图地形。忠实基线必须覆盖：

- OCBT 位域、压缩求和树和四档容量特化；
- 动态二分器物理槽位池；
- 物理槽位与逻辑 `heapID` 的分离；
- 三邻接双缓冲；
- 屏幕面积分类、背面剔除和视锥剔除；
- split 兼容链、保守容量预留、空闲 rank-select、四种二分模板和邻接传播；
- simplify 合法性检查、merge、槽位释放和邻接传播；
- 三段 OCBT 归约、活动索引和 GPU 间接命令；
- 最小平面几何验证和最终高度图地形几何求值；
- 无普通帧同步读回的统计、验证和 benchmark 路径。

完成后，当前主分支应具备三套可运行路径：

```text
Classic CPU ROAM
Data-Oriented CPU ROAM
CBT 2024 官方语义 GPU 基线
```

兼容闭包感知预算调度器不属于本计划的忠实基线。只有阶段 I 冻结官方语义基线后，才允许建立新的算法标识、shader 和 benchmark 标签实现研究变体。

## 2. 本次源码复核得到的不可变事实

本节不是建议架构，而是 `large_cbt` 当前固定提交的实际行为。接入实现如果有意偏离，必须在代码、统计和实验标签中明确记录。

### 2.1 上游真实帧事务

上游 [`MeshUpdater::update`](../../third_party/large_cbt/demo/src/mesh/mesh_updater.cpp) 的一轮更新顺序是：

```text
Reset
  ↓
Classify
  ↓
PrepareIndirect(split) → Split
  ↓
PrepareIndirect(allocate) → Allocate
  ↓
Copy currentNeighbors → nextNeighbors
  ↓
Bisect → PrepareIndirect(propagate split) → PropagateBisect
  ↓
PrepareSimplify → PrepareIndirect(simplify) → Simplify
  ↓
PrepareIndirect(propagate simplify) → PropagateSimplify
  ↓
ReducePre → ReduceFirst → ReduceSecond
  ↓
交换 currentNeighborsBufferIdx
  ↓
BisectorIndexation → PrepareBisectorIndirect
```

split 和 simplify/merge 最终属于同一个邻接代次和同一次 Reduce。阶段 E 可以用 split-only 作为开发检查点，但正式忠实基线不能把 split 与 merge 组织成两条彼此独立、顺序不同的永久管线。

### 2.2 分类消费上一轮活动索引和完整几何

上游 `Classify` 不是扫描全部容量，而是通过上一轮生成的：

- `indexedBisectorBuffer`；
- `indirectDispatchBuffer[0..2]`；
- `indirectDrawBuffer[9]` 的显式活动二分器数；

间接调度当前活动二分器。每个槽位读取三个当前顶点和一个父级辅助位置：

```text
currentVertexBuffer[3 * slot + 0..2]
currentVertexBuffer[3 * totalNumElements + slot]
```

因此，动态 split 接入前必须先有可供分类读取的最小几何 bootstrap。不能等到所有拓扑阶段完成后才第一次实现几何求值。

### 2.3 分类语义是像素面积，不是 CPU ROAM 的像素厚度

上游分类顺序为：

1. 背面剔除；
2. 三角形 AABB 视锥剔除；
3. 投影三个顶点；
4. 计算像素面积；
5. 按观察角度放大掠射角面积；
6. 与 `_TriangleSize`、`0.5 * _TriangleSize` 和 `_MaxSubdivisionDepth` 比较。

它与 Classic/DOD 使用的 nested wedgie 像素误差不是同一指标。CBT 必须增加独立的 `TriangleAreaPixels` 参数，不得把现有 `ScreenSpaceSplitThresholdPixels` 直接解释成同一含义。

### 2.4 split 规划、分配和提交是三个不同事务

上游 `SplitElement` 只做兼容链规划和容量预留：

- 最大保守需求为 `2 * (currentDepth - baseDepth) - 1`；
- 边界节点特化为 1；
- 互为 facing twin 的直接二分特化为 2；
- 先原子扣减剩余容量，失败时恢复；
- 用原子 `OR subdivisionPattern` 认领兼容链节点；
- 重叠链通过分布在各节点上的 pattern 去重；
- 把实际需要分配的节点写入 `allocateBuffer`；
- 归还保守预留中未使用的部分。

`Allocate` 再通过唯一的空闲 rank 区间调用 `decode_bit_complement`，只写 `BisectorData.indices`。此时不能提前设置 OCBT 位，因为同一批线程必须基于同一份旧树执行空闲 rank-select。

`Bisect` 最后才提交 `heapID`、下一代邻接、修改标记、传播任务和新槽位 OCBT 位。

### 2.5 四种 split 模板必须保持原始语义

上游有效模板是：

| pattern | 新增槽位数 | 提交后活动节点数 |
|---|---:|---:|
| `CENTER_SPLIT` | 1 | 2 |
| `RIGHT_DOUBLE_SPLIT` | 2 | 3 |
| `LEFT_DOUBLE_SPLIT` | 2 | 3 |
| `TRIPLE_SPLIT` | 3 | 4 |

模板决定保留槽位的新 `heapID`、新槽位 `heapID`、三个邻接、`problematicNeighbor` 和传播任务。它们必须先逐模板通过 CPU 参考和 GPU 读回测试，再允许组合运行。

### 2.6 邻接代次的真实规则

上游在 `Bisect` 前把完整 `currentNeighbors` 复制到 `nextNeighbors`：

- split 模板读取当前代，写下一代；
- `PropagateBisect` 修改下一代；
- `PrepareSimplify`、`Simplify` 和 `PropagateSimplify` 继续读取/修改下一代；
- Reduce 后才将 `nextNeighbors` 发布为新的 current。

这不是“每个 pass 轮换一次邻接缓冲”，而是“一帧完整拓扑事务只发布一个新邻接代次”。

### 2.7 上游间接命令布局是 10 个 `uint`

上游 `indirectDrawBuffer` 不是三个连续 `D3D12_DRAW_ARGUMENTS`，其精确布局是：

| `uint` 偏移 | 含义 |
|---:|---|
| 0..3 | 活动二分器 `D3D12_DRAW_ARGUMENTS` |
| 4..7 | 可见二分器 `D3D12_DRAW_ARGUMENTS` |
| 8 | 修改几何顶点数，按每个二分器 4 个位置计数 |
| 9 | 显式活动二分器数 |

`indirectDispatchBuffer` 是三组 `D3D12_DISPATCH_ARGUMENTS`，分别服务活动二分器、活动几何位置和修改几何位置。

上游当前渲染代码实际绑定活动索引和第一组 draw 参数。可见索引虽然生成，但没有替代活动列表成为默认绘制输入。忠实基线先保留这个行为；如果以后改为只绘制可见列表，必须作为独立适配差异记录。

### 2.8 上游验证能力有限

上游 `Validate` 主要检查活动节点邻接是否能双向找到引用。它不验证 OCBT 位、`heapID`、索引唯一性、间接命令计数或容量守恒。RoamTesting 可以增加更严格的验证，但不能把扩展验证误写成上游已有能力。

### 2.9 不迁移上游每帧 queue flush

上游演示程序在帧尾 flush 队列，方便立即读取验证和占用量，但会串行化 CPU/GPU。RoamTesting 只迁移 GPU pass 之间的依赖、UAV barrier 和资源状态转换；普通帧继续使用现有双帧围栏和延迟读回。

## 3. 当前仓库实现审计

### 3.1 已完成且可复用

- `TerrainLodAlgorithmId::Cbt2024`、可用性和 capability gate；
- 与 renderer 同源的 View、Projection、ViewProjection、视锥和平面尺寸输入；
- `GpuProceduralIndirect`、活动索引 SRV 和 `D3D12_DRAW_ARGUMENTS`；
- SM 6.6、shader int64 和 64 位 UAV 原子能力检查；
- CPU OCBT 参考结构；
- 128K、256K、512K、1M GPU OCBT 测试 shader 和 CPU/GPU 对照；
- 六个方形基础半边、`heapID=8..13`、`prev/next/twin` 和 18 个基础控制点；
- OCBT、拓扑、任务、索引和基础命令资源的初始创建与容量切换 smoke；
- 六个基础二分器的程序化间接绘制。

### 3.2 E0 前审计项与完成结果

| 位置 | E0 前行为 | E0 完成结果 |
|---|---|---|
| `TerrainRenderer::UpdateForView` | CBT 只在平移超过阈值或 mesh dirty 时 Build；纯旋转可能不更新 | 已增加 `EveryFrame` policy；300 帧 smoke 逐帧校验 topology generation，覆盖 150 帧静止和 150 帧纯旋转 |
| `TerrainLodRenderPacket` | GPU 模式要求 CPU 提供大于零的实时活动数，并用它判断 drawable | 已由 GPU 间接命令拥有真实 draw count；CPU 活动数为零时资源契约仍可绘制 |
| `CbtTopologyBufferLayout` | draw buffer 被建模为三个 draw command | 已改为上游精确 10 `uint` draw state，并增加独立 9 `uint` geometry dispatch |
| `D3D12CbtGpuState` | 没有 `memoryBuffer`、`validationBuffer` 和内部 topology indirect scratch | 已补齐 memory、validation、topology dispatch、geometry dispatch 和精确初始化数据 |
| OCBT HLSL | rank-select 已验证，Reduce 实现仍位于测试 shader | 测试与生产入口已共用 `CbtReducePre/First/Second` |
| CBT terrain adapter | CPU 只写六个基础三角形，顶点容量为每槽 3 个 | 已由 GPU Bootstrap 生成四位置分类布局和 52-byte render vertex |
| 资源同步 | 基础 adapter 使用持久 upload heap；没有计算/绘制状态机 | topology 和几何已迁至 default heap 常驻，并集中维护 UAV、SRV 和 `INDIRECT_ARGUMENT` 状态 |
| 统计 | 只有统一 CPU ROAM 字段 | 已增加 topology frame generation 和零普通帧算法读回口径；专用计数、分阶段 GPU 时间和延迟样本仍属于阶段 H |
| 测试 | GPU smoke 是四档容量穷举长测试，D3D12 CTest 未形成快测闭环 | 已增加 128K、256K、512K、1M 四档 `gpu-quick`/300 帧 CTest 与 90 秒超时 |

### 3.3 基础阶段 D 的完成边界

阶段 D 的“已完成”只表示基础拓扑、资源生命周期和固定绘制已经验证，不表示当前资源二进制布局已经满足完整 `MeshUpdater`。`memoryBuffer`、验证资源、精确 draw 状态和两类 dispatch buffer 在阶段 E0 补齐，不回写阶段 D 的历史完成记录。

## 4. 实施原则

### 4.1 先复现，后改进

忠实基线阶段不得改变：

- 像素面积分类和 0.5 倍 simplify 阈值；
- 原子追加候选顺序；
- split 保守容量预留公式；
- `subdivisionPattern` 的认领与共享链去重；
- 旧 OCBT 上执行空闲 rank-select、Bisect 后置位；
- 四种 split 模板；
- simplify 局部合法性检查；
- 邻接传播、Reduce 和 Indexation 顺序。

全局排序、收益评分、闭包共享成本去重和 split/merge 预算交换必须进入独立研究算法，不能直接覆盖基线 shader。

### 4.2 迁移算法，不迁移演示引擎

继续复用：

- `Application`；
- `D3D12GraphicsBackend`；
- `TerrainRenderer`；
- `ImGuiLayer`；
- 现有纹理、benchmark、统计和双帧围栏框架。

不迁移上游窗口、天空、水体、月球材质、自研 DX12 backend 或每帧 flush。

### 4.3 普通帧保持 GPU 常驻和异步

普通帧不允许：

- 从 Classic/DOD 或 CPU 快照重建 CBT 拓扑；
- 将活动拓扑整体读回 CPU；
- 每帧创建/销毁拓扑资源或 PSO；
- 为获得活动数或计时结果同步等待 GPU；
- 用 `ExecuteImmediate` 提交普通拓扑更新。

首次初始化、容量切换、高度图替换和设备重建允许显式 GPU idle，但必须作为控制事件单独统计，不能混入稳定帧 benchmark。

### 4.4 拓扑、逻辑几何和最终地形顶点分层

正式路径保持三个可独立计时和验证的阶段：

```text
CBT topology
  → heapID 对应的平面二分参数位置
  → 高度图采样、法线和 TerrainMeshVertex
  → procedural indirect draw
```

阶段 E0 先实现最小平面几何 bootstrap，阶段 G 再接完整高度图求值。不能将高度图采样塞进 split/merge shader。

### 4.5 扩展验证可以更强，候选策略不能偷偷变化

RoamTesting 可以补充上游缺少的 OCBT、`heapID`、索引和容量不变量。验证 pass 只观察结果，不参与候选获批、槽位分配或邻接提交。

## 5. 修正后的目标架构

```text
Application
└── TerrainRenderer
    ├── Classic CPU ROAM
    ├── Data-Oriented CPU ROAM
    └── D3D12CbtTerrainLodAlgorithm
        ├── CbtGpuState
        │   ├── OCBT tree + bitfield
        │   ├── heapID + neighbors[2] + current neighbor generation
        │   ├── bisector data + classification/allocate/simplify/propagate
        │   ├── memory counters + validation counters
        │   ├── active/visible/modified indices
        │   ├── topology indirect scratch
        │   ├── draw state + geometry dispatch arguments
        │   ├── base control points + parametric positions
        │   ├── height-map resource + final TerrainMeshVertex buffer
        │   └── timestamp/readback ring
        ├── CbtTopologyPipeline
        │   └── Reset → Classify → Split → Allocate → Bisect
        │       → PropagateBisect → PrepareSimplify → Simplify
        │       → PropagateSimplify → Reduce → Indexation
        ├── CbtTerrainGeometryPipeline
        │   └── heapID → planar LEB coordinates → height sample
        │       → normal/UV/debug attributes
        └── TerrainLodRenderPacket
            └── final vertex SRV + active index SRV + GPU draw arguments
```

类名是职责建议，不要求一次拆出所有公开类型。以下边界必须存在：

- GPU 资源所有权只在 CBT 算法状态内；
- renderer 只借用最终绘制所需的三个资源；
- topology pipeline 可以访问后端当前帧命令列表，但不能 Present 或等待；
- smoke/validation 可以调用专用阻塞读回路径，普通 `BuildRenderData` 不可以。

## 6. 接口、生命周期和调度契约

### 6.1 每帧更新策略

当前统一 capability 需要增加明确的更新策略，例如：

```text
TerrainLodUpdatePolicy::OnDemand       Classic/DOD
TerrainLodUpdatePolicy::EveryFrame     动态 CBT
```

动态 CBT 必须在每个 `BeginFrame` 与 `Present` 之间调用一次 `BuildRenderData`，原因包括：

- 纯相机旋转会改变分类；
- 同一视图下拓扑可能需要多帧收敛；
- 上一轮活动索引和下一轮间接调度需要连续发布；
- 延迟统计和资源状态机按帧推进。

固定基础 adapter 在阶段 E0 完成前可继续按需更新。

### 6.2 GPU 拥有 draw count

`GpuProceduralIndirect` 的正确性不得依赖 CPU 实时活动数。渲染包应区分：

- GPU 资源容量；
- GPU 间接命令位置和格式；
- 最近一个已完成延迟样本中的活动数；
- 资源代次。

`HasConsistentResourceContract` 应根据资源、stride、容量、间接命令和生命周期判断是否可绘制。`ActiveTriangleCount` 可以保留为延迟统计，但不能要求其大于零，也不能用于决定是否执行间接绘制。

### 6.3 资源 generation 与 topology generation 分离

- `GpuResourceGeneration`：仅在资源对象、容量或描述符需要重建时递增；
- `TopologyFrameGeneration`：每次成功记录并发布拓扑事务递增，仅供诊断；
- 普通 split/merge 不应使 renderer 重写 SRV 描述符；
- 容量切换必须等待旧资源最后一个 fence 完成后再释放。

### 6.4 命令记录位置

普通更新直接记录到 `D3D12GraphicsBackend::CommandList()`：

```text
Application::RenderFrame
  → BeginFrame
  → TerrainRenderer::UpdateForView
  → CBT RecordUpdate / RecordGeometry
  → TerrainRenderer::Render
  → Present
```

`ExecuteImmediate` 只用于初始化、容量切换和专用阻塞测试。生产资源初始化如果发生在已打开帧中，必须在文档和统计中标记为一次性控制事件；不能在之后的稳定帧重复发生。

### 6.5 参数必须按算法分义

CBT 至少需要：

- `CbtCapacity`：128K、256K、512K、1M；
- `CbtTriangleAreaPixels`：官方 `_TriangleSize`；
- `MaxDepth`：限制到 64 位 `heapID` 可表示范围并且不低于 `BaseDepth`；
- `CbtValidationMode`：Off、Delayed、BlockingSmoke；
- `CbtGeometryUpdateMode`：ModifiedOnly、FullDebug。

CPU ROAM 的厚度阈值和 triangle budget 继续保留原义，不伪装成 CBT 官方参数。

## 7. GPU 常驻资源与精确二进制协议

设动态容量为 `N`，方形基础半边数为 `B=6`，总物理槽位 `T=N+B`。

### 7.1 拓扑和任务资源

| 资源 | 元素/大小 | 初值与作用 |
|---|---:|---|
| OCBT tree | 容量特化 | 根计数和压缩子树计数 |
| OCBT bitfield | `N / 64` 个 `uint64` | 只管理动态槽位 `[0,N)` |
| `heapID` | `T × uint64` | 动态前缀为 0；基础尾部为 8..13 |
| `neighbors[2]` | `2 × T × uint3` | 初始两份相同；每帧发布一个新代次 |
| `bisectorData` | `T × 32 B` | 与上游 `BisectorData` 二进制一致 |
| classification | `(2 + 2T) × uint` | split/simplify 两个计数和两段候选 |
| allocation | `(1 + T) × int` | 计数加待分配节点 |
| simplification | `(1 + T) × uint` | 合法 merge 发起节点 |
| propagation | `(2 + T) × int` | split/merge 两个计数共享任务区 |
| memory | `2 × int` | 实际分配 rank 游标、可预留剩余槽位 |
| validation | 至少 `2 × uint` | 错误计数和首错诊断索引 |

`CbtBisectorData` 的默认无效字段必须初始化为 `InvalidCbtBisectorIndex`，不能依赖清零等价于无效指针。

跨语言 ABI 的标志位、分类值、模板位、结构字数和字段 word offset 统一来自
[`CbtGpuAbi.shared.h`](../../src/algorithms/cbt_2024/CbtGpuAbi.shared.h)。C++ 适配层使用 `sizeof`/`offsetof`
静态断言锁定布局，两个生产 HLSL 入口则共同包含 [`CbtGpuAbi.hlsli`](../../assets/shaders/dx12/cbt/CbtGpuAbi.hlsli)，不再各自复制 `CbtBisectorData` 与 draw-state 索引。

### 7.2 索引和命令资源

| 资源 | 大小 | 作用 |
|---|---:|---|
| active indices | `T × uint` | 下一帧 Classify 和官方默认绘制输入 |
| visible indices | `T × uint` | 忠实生成；基线暂不替代 active draw |
| modified indices | `T × uint` | 增量几何求值 |
| topology indirect scratch | `9 × uint` | split、simplify、allocate、propagate 内部间接调度 |
| geometry dispatch arguments | `9 × uint` | 活动二分器、活动位置、修改位置三组 dispatch |
| draw state | `10 × uint` | 两组 draw args、修改位置数、显式活动数 |

当前 `DrawCommands[3]` 必须在动态管线接入前替换为显式的 10 `uint` 结构，避免 C++、HLSL 和 `ExecuteIndirect` 对偏移产生不同理解。

### 7.3 几何资源

阶段 E0 使用：

- `baseControlPoints`：18 个方形基础半边控制点；
- `classificationPositions`：每槽四个 `float3`，布局与上游三个子位置加父位置一致；
- `renderVertices`：每槽三个 `TerrainMeshVertex`，只供当前 renderer 的程序化顶点 shader；
- 平面几何 compute：从 `heapID` 和基础控制点生成 UV/平面位置，并写入上述两个缓冲。

阶段 G 再增加：

- GPU 高度图纹理和 SRV；
- 高度采样、法线、UV、调试颜色；
- modified-only 和 full-debug 两种几何更新模式。

分类只依赖 `classificationPositions`，renderer 只借用 `renderVertices`，避免让 52 字节渲染顶点结构污染上游拓扑协议。

### 7.4 常量、描述符和 PSO

- 每个交换链帧资源拥有独立 CBT 常量上传区，不能覆盖仍在执行的上一帧常量；
- topology root signature 必须覆盖 `b0..b2`、`t0..t1` 和 `u0..u16`；
- 当前共享 CBV/SRV/UAV heap 只有单槽分配接口，阶段 E0 必须增加连续 descriptor range，或明确采用满足 64 DWORD 限制的 root descriptor 方案；
- 构建期按四档容量编译完整拓扑 entry point，运行期只为当前显式容量创建一组 PSO；
- 生产 OCBT 与测试 OCBT 必须共用同一份 rank-select 和 Reduce 实现，不能维护两套算法副本。

## 8. 修正后的单帧资源依赖

### 8.1 Bootstrap

每次新资源代创建后，在第一次动态分类前完成：

```text
上传基础 heapID / neighbors / control points
  → 初始化 bisectorData 无效字段和基础 flags
  → BisectorIndexation
  → PrepareBisectorIndirect
  → 全量平面 LEB 几何求值
  → 生成基础 renderVertices
```

没有这一步，第一帧 `Classify` 会读取未初始化几何或空活动调度参数。

### 8.2 正式帧顺序

```text
上传本帧视图和 CBT 参数
  → Reset counters
  → Classify(previous active indices + positions)
  → PrepareIndirect(split/simplify)
  → Split plan
  → PrepareIndirect(allocate)
  → Allocate from old OCBT
  → Copy neighbors current → next
  → Bisect + set new bits
  → Prepare/PropagateBisect
  → PrepareSimplify
  → Prepare/Simplify + clear released bits
  → Prepare/PropagateSimplify
  → ReducePre/First/Second
  → publish next neighbor generation
  → Indexation
  → Prepare geometry dispatch/draw
  → evaluate modified planar geometry
  → height-map/render-vertex evaluation
  → optional validation and delayed stats copy
  → transition SRV/INDIRECT resources
  → ExecuteIndirect draw
```

### 8.3 D3D12 状态和屏障门槛

实现必须维护集中式资源访问表，至少覆盖：

- compute 写入使用 `UNORDERED_ACCESS`；
- 邻接整表复制使用 `COPY_SOURCE`/`COPY_DEST`，复制后恢复 UAV；
- active indices、classification positions 和 render vertices 在消费时进入 SRV 状态；
- draw/dispatch 参数在执行时进入 `INDIRECT_ARGUMENT`，修改前恢复 UAV；
- 每个 producer/consumer 边界对真正写入的资源建立 UAV barrier，不能只给同一 pass 中另一个资源加 barrier；
- `Bisect` 和 `Simplify` 修改位域后，Reduce 前建立位域 UAV 依赖；
- Reduce 三段之间建立 tree UAV 依赖；
- Indexation 完成后，draw state、indices 和 geometry dispatch 在消费者前建立依赖。

## 9. 分阶段实施计划

### 阶段 A：官方程序构建与基线准备

状态：官方程序和本机兼容版本已运行；完整动态实验冻结留到阶段 I。

已完成：

- 固定上游提交和本机兼容补丁；
- 验证 RTX 设备、SM 6.6 和 64 位原子能力；
- 构建运行 `outer_space`；
- 确认核心 CBT shader 自初始公开提交后没有算法性变化。

阶段 I 仍需固定官方相机、分辨率、容量、截图、占用量和阶段时间。

### 阶段 B：公共接口与程序化绘制

状态：已完成，2026-07-19。

完成内容：

- CBT 算法标识和 capability；
- 完整视图输入；
- `GpuProceduralIndirect`；
- `D3D12_DRAW_ARGUMENTS` 命令签名；
- 活动索引和最终顶点 SRV；
- 设备能力 gate；
- 六基础二分器固定绘制 smoke。

动态阶段补充项已移动到 E0：每帧更新策略和 GPU-owned draw count 契约。

### 阶段 C：OCBT 数据结构

状态：测试范围已完成，2026-07-19；生产接口提升属于 E0。

已经验证：

- 四档 CPU 压缩布局；
- 原子置位/清位；
- occupied/free rank-select；
- 三段 GPU Reduce；
- 空、满、边界、交替和随机变更；
- CPU/GPU 结果一致。

E0 需要把测试 shader 中的 Reduce 提升为生产共用函数，并增加根计数读取和可选组共享缓存，不重新发明第二套 OCBT。

### 阶段 D：方形基础半边和 GPU 基础资源

状态：基础范围已完成，2026-07-20。

已经验证：

- 两个逆时针根三角形展开为六个半边二分器；
- `prev`、`next`、`twin` 和四条边界；
- 基础物理槽位位于动态容量尾部；
- `heapID=8..13`、`BaseDepth=4`；
- 18 个基础控制点；
- 四档容量创建、读回和切回 128K；
- 固定程序化绘制。

动态生产资源的协议差异由 E0 扩展，不改变上述基础拓扑结论。

### 阶段 E0：动态管线前置契约

状态：已完成，2026-08-22。

完成内容：

1. 增加 `EveryFrame` 更新策略，修复纯旋转和静止多帧收敛；
2. 修改 GPU render packet，使间接 draw count 由 GPU 拥有；
3. 将 draw state 改为精确 10 `uint` 布局；
4. 区分 topology indirect scratch 与 geometry dispatch arguments；
5. 增加 memory、validation 和 per-frame constants；复用后端按帧 timestamp/readback，算法普通帧不新增同步读回；
6. 将 OCBT Reduce 提升到生产 HLSL；
7. 建立 topology root signature、当前容量 PSO 和 descriptor range；
8. 建立集中式 D3D12 资源状态/屏障辅助；
9. 实现 Bootstrap Indexation 和最小平面 LEB 几何；
10. 将初始化/容量切换与稳定帧统计分离。

完成条件：

- 六个基础二分器可以连续运行 300 帧，每帧记录空拓扑事务并正常间接绘制；
- 纯相机旋转会触发 CBT Build；
- renderer 不依赖 CPU 实时活动数；
- 普通帧 fence wait 和 readback bytes 为 0；
- Debug Layer、GPU validation 和资源状态检查无错误；
- 四档容量 quick smoke 均在 CI 限时内退出。

验收记录：

- Debug D3D12 完整构建成功，三套 Bootstrap shader 与四档容量的 Reset/Reduce PSO 全部由 DXC 编译；
- `unit;cbt` 三项测试通过：OCBT、基础二分拓扑和 GPU render packet 资源契约；
- `cbt_procedural_e0_quick` 在 128K 容量下完成 300 帧，逐帧 generation 连续；
- `--cbt-base-topology-smoke-test` 通过 128K、256K、512K、1M 以及切回 128K；
- `--cbt-ocbt-smoke-test` 的四档 empty、full、boundary、alternating 和 random-mutation CPU/GPU 对照全部通过；
- 全仓库 14 项 CTest 通过，包括 CPU ROAM、视图投影、增量 mesh 和注释覆盖门禁。

E0 只建立稳定的空拓扑帧事务和基础平面 Bootstrap；`SupportsSplit`、`SupportsMerge`、`SupportsCrackFix` 与 `SupportsTopologyValidation` 继续保持 `false`。

### 阶段 E1：Reset、Classify 和间接工作量

状态：已完成，2026-08-22。

完成内容：

- 迁移官方面积分类、背面剔除、AABB 视锥剔除和掠射角放大；
- 使用上一轮 active indices 间接调度；
- 生成 split/simplify 两段候选；
- 生成内部 split/simplify dispatch args；
- 只读回小型计数头进行延迟诊断。

完成条件：

- 固定平面几何下 CPU 参考与 GPU 分类结果一致；
- 分辨率变化按像素面积缩放；
- 纯旋转和视锥外场景候选变化正确；
- 候选数组不溢出，原子计数与有效条目一致。

验收记录：

- `CbtClassification` CPU 参考覆盖面积公式、分辨率缩放、背面、视锥和最大深度迟滞；
- 四档容量的 `Classify` 与 `PrepareClassificationIndirect` 均由 DXC 编译并进入正式 PSO；
- `cbt_procedural_e1_quick` 在 128K 容量下连续运行 300 帧，静止和纯旋转阶段都保持拓扑 generation 连续；
- 验证模式按 swap-chain 槽延迟读取 8-byte split/simplify 计数，不增加帧内 GPU wait；
- 每个已完成 GPU 样本都与同帧 CPU 参考计数一致，默认视角观察到非零 split 候选；
- 候选计数未超过活动 base list，shader 溢出与无效物理槽位守卫未触发；
- `source_comment_coverage` 同时统计 `.h/.cpp/.hlsl/.hlsli`，shader 注释率为 16.0%。

E1 只产出分类和内部间接工作量，不提交 split；因此 `SupportsSplit`、`SupportsMerge`、`SupportsCrackFix` 与 `SupportsTopologyValidation` 仍保持 `false`。

### 阶段 E2：split 规划与槽位分配

状态：已完成，2026-08-22。

完成内容：

- 迁移前置兼容路径检查；
- 迁移保守容量公式和边界/twin 特化；
- 原子认领 `subdivisionPattern`；
- 记录共享兼容链和重复认领；
- 建立 allocation 列表；
- 用旧 OCBT 空闲 rank-select 分配 `indices[3]`；
- 验证预留、实际使用和返还守恒。

完成条件：

- 单边界、直接 twin、长兼容链和共享兼容链的 pattern 与 CPU 参考一致；
- 容量恰好足够时成功，差一个槽位时完整拒绝；
- 分配槽位唯一且全部位于 `[0,N)`；
- Allocate 结束前 OCBT 位域保持不变。

验收记录：

- `CbtSplitPlanner` CPU 参考逐句对应上游 `SplitElement` 的路径拥有权、保守预留、pattern 原子认领、共享链去重和返还公式；
- CPU 单测覆盖单边界、直接 facing twin、两次降深的长兼容链、共享终点，以及保守容量恰好满足和少一个槽位；
- CPU allocation 参考在带占用洞的旧 OCBT 上验证 free rank-select，所有槽位唯一、未占用，且 packed tree/bitfield 逐字不变；
- 四档容量的 `SplitE2`、`PrepareAllocationIndirect` 和 `AllocateE2` 均由 DXC 编译并进入正式 PSO；
- `cbt_procedural_e2_quick` 连续运行 300 帧，逐样本对照 CPU base pattern、allocation 节点数、slot 数和返还后预算；
- 验证模式确认 GPU 分配槽位为旧空树的唯一 free rank，shader 错误头为零，Reduce 后 OCBT 根仍为零；
- validation 资源记录重复候选、共享兼容节点、总遍历步数和最大链长；常规延迟诊断复制 44 bytes，验证模式复制 240 bytes，均复用 swap-chain frame fence 而不新增等待。

E2 只写 `subdivisionPattern` 和预分配 `indices`，不写新 `heapID`、邻接或 OCBT 位；因此仍不宣称实际 split，四项 topology capability 保持 `false`。

### 阶段 E3：Bisect、传播、Reduce 和 Indexation

状态：已完成。

任务：

- 分别实现四种 split 模板；
- 复制并写入下一代邻接；
- 设置新 `heapID` 和 OCBT 位；
- 生成并执行 split 传播；
- 执行三段 Reduce；
- 发布邻接代次；
- 重建 active/visible/modified indices 和间接命令；
- 为修改节点执行平面几何更新。

完成条件：

- 四种模板分别通过 GPU 读回；
- 连续相机移动可以稳定细分；
- 活动位、非零 `heapID`、active indices 和 draw args 一致；
- 邻接对称，所有索引在范围内；
- 容量永不越界；
- 此阶段明确标记为 split-only 开发基线，不进入正式性能排名。

验收记录：

- 新增 `CbtBisectCommit` CPU 参考，逐字段对应上游四种 `BisectElement` 模板与 `PropagateBisectElement`；单测分别覆盖 center、right-double、left-double、triple、开放边界传播，以及 LEB child/parent 平面坐标；
- 正式 GPU 帧事务现按 `CopyNeighbors -> Bisect -> PropagateBisect -> Reduce -> Indexation -> PrepareIndirect -> BuildModifiedGeometry -> Validate` 执行，E2 预分配槽只在 Bisect 成功写完 `heapID`、邻接和元数据后置位；
- 邻接使用 u3/u4 ping-pong 代次；规划只读已发布代次，模板写下一代，传播按单个边分量原子条件替换，避免并行任务对同一 `uint3` 的读改写覆盖；
- RoamTesting 的开放方形边界允许 `INVALID` 外侧邻居，center/right-double 的边界传播会作为“无需修补”完成；其他越界或非活动 problem neighbor 仍由 shader validation 拒绝；
- 四档容量均成功编译 Copy/Bisect/Propagation/Validate PSO；验证 pass 全槽检查动态 OCBT 位与非零 `heapID` 一致、活动邻接在范围内且互反，并核对 Reduce root、active indices、draw args、allocation/commit/propagation 和四模板计数守恒；
- modified index 间接 dispatch 按 `heapID` 解码平面 LEB child 和 parent 三角形，更新三个绘制顶点与分类所需的第四个父级辅助位置；
- `cbt_procedural_e3_quick` 在 RTX 5090 D / D3D12 上连续运行 300 帧并以退出码 0 结束；前 150 帧静止、后 150 帧旋转相机，延迟 GPU 回读实际观察到四种 Bisect 模板，期间无邻接、OCBT、heap/indexation 或 draw 参数错误；
- 常规延迟诊断为 68 bytes，验证模式为 304 bytes，继续复用 swap-chain frame fence，不增加同步等待；注释率门禁同时统计 `.hlsl` 和 `.hlsli`。

E3 对外启用 `SupportsSplit`、`SupportsCrackFix` 和 `SupportsTopologyValidation`，`SupportsMerge` 保持 `false`。当前仍是 split-only 开发基线，不进入正式性能排名。

### 阶段 F：simplify/merge 与正式帧闭环

任务：

- 在 E1 产生的 simplify 候选上实现 `PrepareSimplify`；
- 检查 pair、facing twin 和四节点同深度条件；
- 由最小逻辑 `heapID` 唯一提交；
- 上移保留槽位 `heapID`，清零删除槽位；
- 清除释放槽位 OCBT 位；
- 生成并执行 merge 传播；
- 将 split 和 merge 固定在同一邻接代次、同一次 Reduce 中；
- 增加最大深度降低和快速相机往返测试。

完成条件：

- 相机往返时拓扑可细化和简化；
- 释放槽位可被后续 split 重新分配；
- 无裂缝、悬空引用或持续占用增长；
- 同帧相邻 split/merge 与上游顺序一致；
- `SupportsMerge` 只有在此阶段验收后才启用；E3 已验收的 split 与裂缝修复能力保持启用。

### 阶段 G：高度图几何求值

任务：

- 根据 `heapID` 和基础控制点解码平面 LEB 坐标；
- 生成三个子位置和一个父级分类位置；
- 将高度图上传为算法持有的 GPU 纹理；
- 采样高度、计算法线、UV 和调试颜色；
- 写入最终 `TerrainMeshVertex`；
- 默认只更新 modified indices，保留 full-debug；
- 验证高度图重载、尺度变化、边界、绕序和纹理方向。

完成条件：

- 固定 `heapID` 集合下 GPU 平面坐标与 CPU 参考一致；
- 高度采样与 `HeightMap::Sample` 在容差内一致；
- 分类读取的三个子位置和父位置全部已初始化；
- 动态更新中无 NaN、未初始化顶点或视觉裂缝；
- 普通帧不再由 CPU 填充整容量顶点上传缓冲。

### 阶段 H：应用、统计和 benchmark

状态：部分完成。

已有：

- 算法选择；
- 可用性提示；
- OCBT、基础拓扑和程序化绘制入口。
- 四档 E3 300 帧 quick smoke、显式 `--cbt-capacity` 参数和 topology frame generation。

待完成：

- 独立 CBT 面积阈值、容量、验证模式和几何模式；
- 专用计数和 GPU 阶段时间；
- 延迟读回样本年龄和 dropped sample 标记；
- 高度图重载、容量切换和算法重置；
- 固定离散相机路径；
- CBT runtime benchmark；

完成条件：CBT 可通过 UI 和命令行稳定运行，自动流程可以无人值守退出，并输出足以复现的参数、硬件、资源代次、计数和时间。

### 阶段 I：官方语义基线冻结

任务：

- 固定 shader、PSO、参数和算法标识；
- 固定 RoamTesting 高度图、分辨率和离散相机路径；
- 记录四档容量结果；
- 与上游程序比较 pass 顺序、占用量趋势和验证结果；
- 记录基础网格、地形几何、帧围栏和额外验证带来的明确差异；
- 建立不可变 Git 提交和 benchmark 标签；
- 在发布或复制上游衍生实现前完成许可证确认。

完成条件：后续研究变体可以与一个不可变、可复现、统计完整、没有隐式 CPU 同步的 CBT 2024 基线比较。

## 10. 统计与实验口径

### 10.1 必须记录的计数

- 容量 `N`、基础槽位 `B`、总槽位 `T`；
- OCBT 根计数和剩余动态槽位；
- active、visible、modified 数量；
- split 和 simplify 分类候选数；
- split 规划获批、容量拒绝和重复认领数；
- 保守预留、实际使用和返还量；
- allocation 列表长度和实际新槽位数；
- 四种 subdivision pattern 数量；
- 兼容链总步数、平均长度和最大长度；
- split/merge 传播任务数；
- merge 提交和释放槽位数；
- 邻接、位域、`heapID`、索引和间接命令错误数；
- 当前资源代次、拓扑帧代次和统计样本年龄。

### 10.2 必须记录的 GPU 时间

- Reset；
- Classify；
- Split；
- Allocate；
- neighbor copy；
- Bisect；
- PropagateBisect；
- PrepareSimplify；
- Simplify；
- PropagateSimplify；
- ReducePre、ReduceFirst、ReduceSecond；
- Indexation；
- planar geometry；
- height/render vertex evaluation；
- validation；
- terrain render。

另行记录 CPU frame-fence wait、控制事件 GPU idle 和统计读回字节，不能把它们混入 shader 阶段时间。

### 10.3 延迟读回规则

- 普通帧只复制固定大小的 counter/timestamp snapshot 到 readback ring；
- 只读取已经由对应 frame fence 完成的槽位；
- 未完成时保留上一份样本并增加 sample age，不等待；
- blocking readback 只允许 smoke/exhaustive validation；
- benchmark 必须标记统计模式，不能把 blocking validation 与正常性能样本混合。

### 10.4 第一轮参数矩阵

| 参数 | 建议取值 |
|---|---|
| CBT 容量 | 128K、256K、512K、1M |
| 三角形面积阈值 | 25、50、100、150 像素面积 |
| 最大深度 | 12、16、20、24，且不低于 BaseDepth |
| 高度图 | `Hm_Terrain_Test_129.pgm`、`Hm_Terrain_Peking_513.png` |
| 分辨率 | 1280×720、1920×1080、2560×1440 |
| 相机 | 固定近地、远近往返、纯旋转、快速横移 |
| 验证 | Off、Delayed、BlockingSmoke |
| 几何 | ModifiedOnly、FullDebug |

所有参数写入 benchmark 文件，不依赖 UI 默认值。

## 11. 测试矩阵与分层

### 11.1 快速 CI

目标：单项秒级、整组有限时、失败可定位。

- CPU OCBT 四档边界和随机小批次；
- GPU OCBT 128K 边界样本和有限 rank；
- 六基础半边和精确命令布局；
- Bootstrap 300 帧；
- 单边界 split；
- 直接 twin split；
- 四种 split 模板独立测试；
- 单个合法 merge；
- 容量差一个槽位拒绝；
- 纯旋转触发更新；
- 三帧延迟统计无等待。

CTest 应使用 `unit`、`gpu-quick`、`integration` 标签并设置明确超时。D3D12 测试构建不能默认关闭后又在文档中声称已由 CTest 覆盖。

### 11.2 Exhaustive 验证

目标：手动或夜间运行，不伪装成快速 smoke。

- 四档容量空/满/交替/随机 OCBT；
- 所有 occupied/free rank 对照；
- 四档基础资源创建、读回和 128K 回切；
- 长兼容链和多候选共享链；
- 同帧相邻 split/merge；
- 最大深度批量降低；
- 长时间相机往返；
- 高度图和容量反复切换；
- GPU validation 和阻塞全不变量读回。

现有 `--cbt-ocbt-smoke-test` 和 `--cbt-base-topology-smoke-test` 应重命名或增加 `--exhaustive` 变体，避免自动化系统误判运行时长。

### 11.3 每轮正确性不变量

1. 动态槽位 `bit=1` 当且仅当对应 `heapID != 0`；
2. 基础槽位 `heapID != 0` 且永不进入 OCBT 位域；
3. OCBT 根计数等于动态非零 `heapID` 数；
4. active indices 恰好包含全部非零 `heapID`，无重复；
5. active draw 顶点数等于 active count × 3；
6. visible 和 modified 列表都是 active 的子集；
7. 所有有效邻接位于 `[0,T)` 且双向可达；
8. `subdivisionPattern` 只包含四种合法组合；
9. 分配槽位互异且不覆盖基础尾部；
10. 预留量守恒：获批预留 = 实际使用 + 返还；
11. merge 释放位可被后续 split 重新选择；
12. 所有活动节点的三个子位置和父位置已初始化且有限；
13. 间接 dispatch/draw 不越过对应缓冲容量；
14. 普通帧不发生同步 CPU 读回或全队列 flush。

## 12. 当前实施批次

### 批次 1：E0 接口和协议

状态：已完成，2026-08-22。

1. 更新策略与 GPU count authority；
2. 精确 draw/dispatch 结构和缺失资源；
3. 生产 OCBT Reduce；
4. root signature、descriptor range、PSO 和资源状态表；
5. Bootstrap Indexation、平面几何和 300 帧空事务测试。

### 批次 2：E1 分类

状态：已完成，2026-08-22。

1. 常量缓冲和视图输入；
2. Reset；
3. Classify；
4. PrepareIndirect split/simplify；
5. CPU/GPU 分类对照和纯旋转测试。

### 批次 3：E2 规划与分配

状态：已完成，2026-08-22。

1. 单边界和直接 twin；
2. 长兼容链；
3. pattern 原子认领与共享链；
4. 保守预留和返还；
5. 空闲 rank-select 分配；
6. 容量边界与唯一性测试。

### 批次 4：E3 split 提交

状态：已完成，2026-08-23。

1. 邻接复制；
2. 四模板 Bisect；
3. PropagateBisect；
4. Reduce；
5. Indexation、几何更新和 split-only smoke。

### 批次 5：F merge 闭环

1. PrepareSimplify；
2. 两节点和四节点合法性；
3. Simplify；
4. PropagateSimplify；
5. split/merge 同帧顺序和槽位回收。

### 批次 6：G-H 高度图和实验接入

1. GPU 高度图；
2. modified/full 几何；
3. 法线、UV 和调试色；
4. 延迟统计；
5. UI、CLI、runtime benchmark 和完整测试矩阵。

每个批次必须先通过自身正确性门槛，再进入下一批。阶段 E3 的 split-only 结果不能代替阶段 F 的正式 CBT 基线。

## 13. 主要风险与控制措施

| 风险 | 影响 | 控制措施 |
|---|---|---|
| 上游仓库缺少许可证 | 无法确定衍生代码发布范围 | 发布或复制实现前确认授权；提交记录区分结构参考和直接迁移 |
| 当前 renderer 不是每帧调用 CBT | 纯旋转失效、拓扑不收敛 | E0 增加明确 update policy |
| CPU live count 与 GPU indirect 冲突 | 普通帧被迫读回或 renderer 错判无内容 | GPU count authority，CPU 只读延迟统计 |
| 当前 draw buffer 布局与上游不同 | shader、C++ 和 ExecuteIndirect 偏移错位 | E0 冻结 10 `uint` 二进制协议和静态断言 |
| 动态 split 先于几何 | Classify 读取未初始化位置 | E0 先建立 Bootstrap 和平面几何 |
| 邻接双缓冲使用错误 | 裂缝、悬空引用 | 一帧只发布一个代次，模板测试加阻塞读回 |
| 对错误资源加 UAV barrier | 间歇性数据竞争 | 集中式资源访问表和 Debug Layer/GPU validation |
| descriptor heap 只支持单槽分配 | 无法稳定绑定大 root table | E0 增加连续 range 或冻结 root descriptor 方案 |
| 四容量 × 多入口导致 shader 数量膨胀 | 构建慢、PSO 管理复杂 | 第一版机械特化并统一命名；语义冻结后再优化构建 |
| 候选原子顺序非确定 | 容量饱和结果波动 | 重复运行并报告分布，不误判为实现错误 |
| exhaustive 测试冒充 smoke | CI 卡住 | quick/exhaustive 分层、标签和硬超时 |
| 高度图与拓扑同时调试 | 错误难定位 | 先平面 E0-F，再在 G 替换几何 |
| 扩展验证影响性能 | 基线计时失真 | Off/Delayed/Blocking 三种明确模式 |

## 14. 不在忠实基线中的工作

- 全局候选排序；
- 连续收益/成本优先级；
- 兼容闭包共享成本去重；
- 固定预算 Top-K 或分桶调度；
- 低收益 merge 与高收益 split 的预算交换；
- 严格确定性的全局资源分配；
- 新的屏幕误差模型；
- 用 visible list 替代上游 active draw 的渲染优化；
- 扫描 OCBT occupied rank 取代上游全容量 Indexation 的性能改写。

这些内容必须在阶段 I 之后进入独立研究变体。

## 15. 忠实基线完成定义

只有同时满足以下条件，才能将 `Cbt2024` 从基础 adapter 标记为完整基线：

1. Reset 到 Indexation 的正式帧顺序与固定上游提交一致；
2. split、merge、传播、Reduce 和槽位回收形成闭环；
3. 三条 GPU 常驻身份关系一致：OCBT 位、物理槽位、逻辑 `heapID`；
4. 动态几何来自 GPU `heapID` 求值和高度图采样；
5. renderer 完全由 GPU 间接命令决定 draw count；
6. 普通帧不 flush、不等待统计读回、不重建整套资源；
7. quick CI、exhaustive validation 和长时间运行均通过；
8. 四档容量、参数、硬件和 benchmark 标签可复现；
9. 所有与上游不同的基础网格、几何、验证和同步策略均有记录；
10. `SupportsSplit`、`SupportsMerge`、`SupportsCrackFix` 和 `SupportsTopologyValidation` 与实际代码能力一致。

## 16. 关键源码索引

| 主题 | 上游源码 | 当前项目对应位置 |
|---|---|---|
| 帧拓扑编排 | [`mesh_updater.cpp`](../../third_party/large_cbt/demo/src/mesh/mesh_updater.cpp) | [`D3D12CbtFramePipeline.cpp`](../../src/algorithms/cbt_2024/d3d12/D3D12CbtFramePipeline.cpp)，已接入 E0-E3 分类、规划、提交、传播、Reduce、Indexation 与增量几何 |
| shader 入口和面积分类 | [`UpdateMesh.compute`](../../third_party/large_cbt/shaders/UpdateMesh.compute) | [`CbtTopologyE0.hlsl`](../../assets/shaders/dx12/cbt/CbtTopologyE0.hlsl) 与 [`CbtClassification.cpp`](../../src/algorithms/cbt_2024/CbtClassification.cpp) CPU 参考 |
| split 规划与分配 | [`update_utilities.hlsl`](../../third_party/large_cbt/shaders/shader_lib/update_utilities.hlsl) | [`CbtTopologyE0.hlsl`](../../assets/shaders/dx12/cbt/CbtTopologyE0.hlsl) 与 [`CbtSplitPlanner.cpp`](../../src/algorithms/cbt_2024/CbtSplitPlanner.cpp) CPU 参考 |
| split 提交、merge、传播 | [`update_utilities.hlsl`](../../third_party/large_cbt/shaders/shader_lib/update_utilities.hlsl) | [`CbtTopologyE0.hlsl`](../../assets/shaders/dx12/cbt/CbtTopologyE0.hlsl) 与 [`CbtBisectCommit.cpp`](../../src/algorithms/cbt_2024/CbtBisectCommit.cpp) 已迁移 split/传播；merge 待 F |
| OCBT | [`ocbt_generic.hlsl`](../../third_party/large_cbt/shaders/shader_lib/ocbt_generic.hlsl) | [`CbtOccupancyTree.hlsli`](../../assets/shaders/dx12/cbt/CbtOccupancyTree.hlsli) |
| 基础半边展开 | [`cpu_mesh.cpp`](../../third_party/large_cbt/demo/src/mesh/cpu_mesh.cpp) | [`CbtBisectorTopology.cpp`](../../src/algorithms/cbt_2024/CbtBisectorTopology.cpp) |
| GPU 资源布局 | [`mesh.cpp`](../../third_party/large_cbt/demo/src/mesh/mesh.cpp) | [`D3D12CbtGpuState.cpp`](../../src/algorithms/cbt_2024/d3d12/D3D12CbtGpuState.cpp) |
| 逻辑几何 | [`PlanetGeometry.compute`](../../third_party/large_cbt/shaders/PlanetGeometry.compute) | [`CbtBootstrap.hlsl`](../../assets/shaders/dx12/cbt/CbtBootstrap.hlsl) 已提供平面 Bootstrap，高度图求值待 G |
| 算法接口 | — | [`ITerrainLodAlgorithm.h`](../../src/algorithms/ITerrainLodAlgorithm.h) |
| 调度与绘制 | — | [`D3D12TerrainRenderer.cpp`](../../src/render/D3D12TerrainRenderer.cpp) |
| 当前 E3 adapter | — | [`D3D12CbtTerrainLodAlgorithm.cpp`](../../src/algorithms/cbt_2024/d3d12/D3D12CbtTerrainLodAlgorithm.cpp) |

## 17. 结论

当前仓库已经完成 OCBT、方形基础半边、程序化间接绘制、阶段 E0 的动态管线前置契约、阶段 E1 的官方面积分类、阶段 E2 的兼容链规划与旧 OCBT 空闲槽位分配，以及阶段 E3 的四模板 Bisect、邻接双缓冲、split 传播、Reduce、Indexation 和平面增量几何。上一帧活动列表、候选、保守预留、共享 pattern、allocation、提交计数、四模板读回和全拓扑验证已经形成可连续运行 300 帧的 split-only 闭环。

下一步进入阶段 F，把 simplify/merge、释放槽位回收和 merge 传播放回上游同一帧顺序，形成可随相机往返细化与简化的忠实闭环；阶段 G 再接入高度图几何求值。这样冻结出的阶段 I 基线才足以支撑后续兼容闭包感知预算调度研究。
