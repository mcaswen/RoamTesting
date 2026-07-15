# large_cbt 总体架构与关键算法路径参考

> 日期：2026-07-16  
> 文档状态：源码架构参考 v0.1  
> 分析对象：`third_party/large_cbt`，提交 `7351e6fc603b9b2c2ab4da399b13a9ab0f327398`  
> 对应论文：*Concurrent Binary Trees for Large-Scale Game Components*，HPG 2024

> 接入实施安排见 [CBT 2024 接入与复现计划](16-cbt-2024-integration-plan.md)。

## 1. 文档目的

本文档用于回答三个问题：

1. `large_cbt` 如何组织 DX12 渲染、GPU 常驻拓扑和自适应几何更新；
2. CBT 2024 的 split、merge、兼容关系传播、内存分配和活动节点索引如何在 GPU 上执行；
3. 哪些部分应忠实复现，哪些部分应通过适配层接入当前 RoamTesting程。

## 2. 一句话概括

`large_cbt` 将一般网格的活动二分器存放在固定容量的 GPU 槽位池中，用 CBT 位域表示槽位占用情况，用紧凑求和树支持“第 i 个已占用槽位”和“第 i 个空闲槽位”的并行查找；每帧通过分类、兼容细分规划、槽位分配、拓扑提交、邻接传播、合法合并、树归约和活动索引重建，完成完全由 GPU 驱动的动态拓扑更新。

## 3. 源码范围与版本判断

官方仓库主干包含以下提交阶段：

| 提交 | 日期 | 含义 |
|---|---:|---|
| `091068c` | 2024-07-01 | HPG 2024 初始公开实现，CBT 拓扑核心首次提交 |
| `92b5ce5` | 2024-09-03 | 加入 Agility SDK 1.614.1、Windows 10 兼容和内存泄漏修复 |
| `7351e6f` | 2025-10-26 | 修改水体模拟和相机路径，不涉及 CBT 核心 |

`UpdateMesh.compute`、`update_utilities.hlsl` 和 `mesh_updater.cpp` 的核心历史只有初始提交。因此当前 HEAD 可以用于复现，同时应在实验记录中固定完整提交编号。

## 4. 总体分层

```text
projects/outer_space.cpp
└── SpaceRenderer                         应用、窗口、帧循环和场景编排
    ├── DX12 backend                      设备、队列、命令、资源和管线封装
    ├── Planet                            单个自适应网格实例及常量缓冲
    │   ├── CPUMesh / BaseMesh            静态基网格和初始邻接
    │   ├── CBTMesh                       GPU 常驻拓扑、几何和间接命令资源
    │   └── PlanetGeometry.compute        由逻辑二分路径生成几何位置
    ├── MeshUpdater                       动态拓扑命令编排
    │   └── UpdateMesh.compute
    │       ├── 分类与候选收集
    │       ├── split 兼容链与内存预留
    │       ├── 槽位分配与二分提交
    │       ├── merge 合法性检查与提交
    │       ├── 邻接传播
    │       └── CBT 归约、活动索引和间接命令生成
    ├── Water/Moon deformation            将逻辑几何变形成最终顶点
    └── Earth/Moon renderer               程序化间接绘制与材质求值
```

### 4.1 目录职责

| 目录 | 主要职责 | 是否属于 CBT 核心 |
|---|---|---|
| `projects/` | `outer_space` 可执行程序入口 | 否 |
| `demo/src/render_pipeline/` | 场景、帧循环、行星实例和渲染编排 | 部分 |
| `demo/src/mesh/` | CPU 基网格、GPU 资源创建、拓扑阶段调度 | 是 |
| `demo/src/cbt/` | CPU 版 OCBT、GPU CBT 上传和容量变体 | 是 |
| `demo/src/graphics/` | 自研 DX12 薄封装 | 基础设施，不应整体迁移 |
| `shaders/UpdateMesh.compute` | GPU 拓扑入口函数和面积分类 | 是 |
| `shaders/shader_lib/update_utilities.hlsl` | split、merge、传播、索引和验证 | 是 |
| `shaders/shader_lib/ocbt_*.hlsl` | 不同容量的 OCBT 布局和归约特化 | 是 |
| `shaders/PlanetGeometry.compute` | 根据二分路径生成行星表面几何 | 场景相关 |
| `shaders/Water/`、`shaders/Moon/` | 水面和月球形变 | 否 |
| `shaders/Visibility/` | 可见性缓冲渲染 | 否 |

## 5. 初始化路径

### 5.1 应用初始化

程序从 [`projects/outer_space.cpp`](../../third_party/large_cbt/projects/outer_space.cpp) 创建 `SpaceRenderer`。`SpaceRenderer::initialize` 负责：

- 创建窗口、DX12 设备、命令队列、交换链和命令缓冲；
- 创建颜色、深度和可见性缓冲；
- 初始化 `MeshUpdater`、着色器、天空、水体和材质模块；
- 选择 OCBT 容量，默认使用 `OCBT_128K`；
- 加载 `models/icosahedron.ccm`，构建地球和月球两个自适应网格实例。

### 5.2 基网格展开

[`cpu_mesh.cpp`](../../third_party/large_cbt/demo/src/mesh/cpu_mesh.cpp) 使用 `ccmesh` 读取一般多边形网格，并按半边生成初始二分器：

- 每条半边对应一个基础二分器；
- `prev`、`next` 和 `twin` 半边转换成三个邻接槽位；
- 每个基础二分器取得一个逻辑堆编号 `heapID`；
- 基础三角形的三个控制点写入 `basePoints`；
- 基础二分器放在动态池之后，动态池槽位初始为空。

总物理槽位数为：

```text
totalNumElements = CBT 动态槽位容量 + 基网格半边数量
```

这个布局意味着基础二分器不占用 CBT 位域预算，CBT 位域只管理后续动态产生的子二分器。

### 5.3 GPU 资源创建

[`CBTMesh`](../../third_party/large_cbt/demo/include/mesh/mesh.h) 是单个自适应网格实例的主要 GPU 状态，初始化逻辑位于 [`mesh.cpp`](../../third_party/large_cbt/demo/src/mesh/mesh.cpp)。其资源可以分为六组：

| 资源组 | 缓冲 | 作用 |
|---|---|---|
| CBT 占用结构 | `gpuCBT.bufferArray[2]` | 压缩求和树和 64 位占用位域 |
| 逻辑拓扑 | `heapIDBuffer` | 物理槽位到逻辑二分路径的映射 |
| 邻接拓扑 | `neighborsBuffers[2]` | 三邻接关系，采用双缓冲更新 |
| 中间任务 | `updateBuffer`、`classificationBuffer`、`allocateBuffer`、`simplificationBuffer`、`propagateBuffer` | 分类、分配、合并和传播任务 |
| 活动索引 | `indexedBisectorBuffer`、`visibleIndexedBisectorBuffer`、`modifiedIndexedBisectorBuffer` | 活动、可见和本帧修改二分器列表 |
| 几何与命令 | `lebVertexBuffer`、`currentVertexBuffer`、`currentDisplacementBuffer`、两个间接命令缓冲 | 逻辑几何、最终几何、形变和 GPU 驱动命令 |

## 6. 核心数据模型

### 6.1 物理槽位与逻辑堆编号分离

官方实现同时维护两套身份：

- **物理槽位 `currentID`**：所有 GPU 数组使用的稳定下标；
- **逻辑堆编号 `heapID`**：编码该二分器位于二分树中的路径和深度。

split 时可以复用任意空闲物理槽位作为新子节点，但新槽位的 `heapID` 由父节点逻辑路径决定。merge 时，被删除槽位的 `heapID` 被清零，保留槽位的 `heapID` 上移到父节点。

这种分离是 GPU 内存池得以紧凑复用的关键：拓扑路径不要求物理数组按树形连续排列。

### 6.2 `BisectorData`

[`BisectorData`](../../third_party/large_cbt/demo/include/cbt/bisector.h) 每个物理槽位一份，主要字段如下：

| 字段 | 含义 |
|---|---|
| `subdivisionPattern` | 本轮应执行的细分模式，可表示中心、左、右及其组合 |
| `indices[3]` | 为新子二分器分配的物理槽位 |
| `problematicNeighbor` | 拓扑提交后需要修复引用的外部邻居 |
| `bisectorState` | 保持、split、simplify 或已 merge |
| `flags` | 是否可见、是否在本轮被修改 |
| `propagationID` | 传播阶段所需的旧父节点或被删除节点编号 |

### 6.3 OCBT 占用结构

OCBT 由两部分组成：

1. 64 位位域：每一位表示一个动态物理槽位是否占用；
2. 压缩求和树：每个内部节点记录子树中的活动位数量。

仓库提供 `128K`、`256K`、`512K` 和 `1M` 四种静态容量。不同深度使用不同位宽存储计数，靠近根节点使用 32 位，靠近叶节点使用更紧凑的计数，最底层直接读取 64 位位域。

OCBT 提供三项关键操作：

- `decode_bit(i)`：找到第 `i` 个已占用槽位；
- `decode_bit_complement(i)`：找到第 `i` 个空闲槽位；
- `reduce()`：位域变化后自底向上重建子树计数。

其中 `decode_bit_complement` 是并行内存分配的核心：线程先取得唯一空闲序号，再通过求和树定位对应物理槽位，不需要链表式空闲表。

## 7. 每帧总体路径

[`SpaceRenderer::render_pipeline`](../../third_party/large_cbt/demo/src/render_pipeline/space_renderer.cpp) 的关键顺序为：

```text
更新相机与常量缓冲
    ↓
判断行星是否可见、是否需要更新
    ↓
MeshUpdater::update                GPU 拓扑更新
    ↓
Planet::evaluate_leb               根据 heapID 生成逻辑几何
    ↓
Water/Moon deformation             生成相机相对最终顶点
    ↓
可选拓扑验证与占用读回
    ↓
可见性缓冲或线框绘制
    ↓
材质计算、界面、提交和呈现
```

需要明确区分：

- `MeshUpdater` 只更新拓扑和活动索引，不直接生成最终地形顶点；
- `PlanetGeometry.compute` 根据逻辑二分路径生成未形变几何；
- 水体或月球形变阶段把逻辑几何转换为最终绘制顶点。

这三个阶段在 RoamTesting 中不应合并成一个不可测量的大计算着色器。

## 8. GPU 拓扑更新路径

官方源码中没有单独名为 `GenerateCommands` 的函数。对应职责分散在 [`MeshUpdater::update`](../../third_party/large_cbt/demo/src/mesh/mesh_updater.cpp) 编排的多个计算阶段中：

```text
Reset
  ↓
Classify
  ↓
PrepareIndirect(split) → Split
  ↓
PrepareIndirect(allocate) → Allocate
  ↓
Bisect → PrepareIndirect(propagate) → PropagateBisect
  ↓
PrepareSimplify → PrepareIndirect(simplify) → Simplify
  ↓
PrepareIndirect(propagate) → PropagateSimplify
  ↓
Reduce
  ↓
BisectorIndexation → PrepareBisectorIndirect
```

### 8.1 `Reset`：重置计数，不清空拓扑

[`ResetBuffers`](../../third_party/large_cbt/shaders/shader_lib/update_utilities.hlsl) 将本轮任务计数器清零，并计算剩余动态槽位：

```text
剩余槽位 = CBT 容量 - CBT 根节点记录的活动位数量
```

`_MemoryBuffer[0]` 是本轮实际分配序号的原子游标，`_MemoryBuffer[1]` 是可预留的剩余槽位数。

### 8.2 `Classify`：面积阈值与候选收集

[`UpdateMesh.compute`](../../third_party/large_cbt/shaders/UpdateMesh.compute) 对当前活动二分器执行：

1. 背面剔除；
2. 三角形包围盒视锥剔除；
3. 投影三个顶点并计算屏幕空间面积；
4. 对掠射角进行面积放大；
5. 根据 `_TriangleSize` 和 `_MaxSubdivisionDepth` 判断 split、simplify 或保持。

split 候选通过 `InterlockedAdd` 追加到 `classificationBuffer`。simplify 候选同样追加到另一段数组，但只登记满足约定的偶数逻辑节点，由后续阶段负责成组检查。

候选数组没有排序、分桶或全局优先级。线程写入顺序由 GPU 调度和原子竞争决定。

### 8.3 `Split`：兼容细分规划和保守内存预留

[`SplitElement`](../../third_party/large_cbt/shaders/shader_lib/update_utilities.hlsl) 不立即修改 `heapID` 和邻接，而是先生成细分计划：

1. 检查当前节点是否已经位于其他候选的兼容路径上，减少重复处理；
2. 按当前深度与基础深度估计兼容链最大槽位需求；
3. 对边界节点和直接 twin 情况使用更小的特殊估计；
4. 原子扣减剩余槽位；
5. 如果容量不足，恢复计数器并放弃此次 split；
6. 原子设置 `subdivisionPattern`，避免多个候选重复认领同一节点；
7. 沿 twin 方向向上遍历，形成中心、双重或三重细分模式；
8. 将真正需要分配新子节点的物理槽位写入 `allocateBuffer`；
9. 归还保守估计中没有实际使用的槽位。

这里的“兼容链”不是一个单独持久数组，而是通过邻接遍历和各节点的 `subdivisionPattern` 分布式表示。

### 8.4 `Allocate`：从第 i 个空闲位取得物理槽位

每个待细分节点根据 `subdivisionPattern` 计算需要的新槽位数量，并通过原子加法从 `_MemoryBuffer[0]` 获取一段唯一序号。随后对每个序号调用 `decode_bit_complement`，得到实际空闲物理槽位，并写入 `BisectorData.indices`。

分配阶段只决定槽位编号，不立即设置占用位。由于所有线程取得的空闲序号互不重复，所以它们基于同一份旧 CBT 位域执行 rank-select 仍不会选中同一槽位。

### 8.5 `Bisect`：提交 heapID、邻接和占用位

[`BisectElement`](../../third_party/large_cbt/shaders/shader_lib/update_utilities.hlsl) 根据 `subdivisionPattern` 执行四种拓扑模板：

- `CENTER_SPLIT`：父节点生成两个子节点；
- `RIGHT_DOUBLE_SPLIT`：生成三个节点；
- `LEFT_DOUBLE_SPLIT`：生成三个节点；
- `TRIPLE_SPLIT`：生成四个节点。

提交内容包括：

- 更新保留槽位和新槽位的 `heapID`；
- 写入下一份邻接缓冲；
- 标记可见和本轮修改节点；
- 记录需要在传播阶段修正的外部邻居；
- 原子设置新物理槽位的 CBT 占用位。

邻接使用双缓冲：提交前先复制当前邻接缓冲到下一缓冲，本轮拓扑模板写入下一缓冲，结束后交换当前索引。这样能避免同一轮中大范围原地覆盖旧邻接。

### 8.6 `PropagateBisect`：修复外部邻居引用

拓扑模板只能直接更新参与本次细分的局部节点。对于仍然指向旧父节点的外部邻居，`PropagateBisectElement` 根据目标邻居本轮采用的细分模式，找到正确的新子节点并替换引用。

该阶段解决的是邻接引用一致性，而不是继续产生新的几何误差候选。

### 8.7 `PrepareSimplify`：验证可合并菱形

merge 不能按单个三角形独立执行。`PrepareSimplifyElement` 检查：

- 当前节点与配对节点深度一致；
- 配对节点也被分类为 simplify；
- 如果存在面对面的 twin 对，则四个节点深度一致；
- 四个节点都要求 simplify；
- 由确定的最小逻辑编号负责提交，避免重复 merge。

只有满足完整局部结构的候选才写入 `simplificationBuffer`。

### 8.8 `Simplify` 与 `PropagateSimplify`

`SimplifyElement` 将保留节点的 `heapID` 上移为父节点，清零被删除节点的 `heapID`，重建局部邻接，并清除被释放物理槽位的 CBT 位。

如果外部邻居仍指向被删除节点，则把任务写入 merge 传播区。`PropagateElementSimplify` 根据邻居本身是否也发生 merge，决定应将引用改到保留节点还是邻居的配对节点。

### 8.9 `Reduce`：重建 OCBT 计数树

split 和 merge 只修改底层占用位。之后必须执行三段归约：

1. `ReducePrePass`：统计 64 位位域块，生成靠近叶层的压缩计数；
2. `ReduceFirstPass`：并行归约中间层；
3. `ReduceSecondPass`：在组共享内存中完成靠近根节点的归约。

归约结束后，根节点重新表示动态池占用量，`decode_bit` 和 `decode_bit_complement` 才能服务下一帧。

### 8.10 活动索引与间接命令生成

`BisectorIndexation` 扫描全部物理槽位：

- `heapID != 0` 的槽位进入活动二分器列表；
- 带 `VISIBLE_BISECTOR` 的槽位进入可见列表；
- 带 `MODIFIED_BISECTOR` 的槽位进入本轮修改列表。

同一阶段用原子计数填充 `indirectDrawBuffer`。随后 `PrepareBisectorIndirect` 生成三组间接调度参数：

| 间接调度 | 消费者 | 工作量 |
|---|---|---|
| 活动二分器调度 | 下一帧分类、完整几何计算 | 活动二分器数 |
| 活动顶点调度 | 水体/月球形变 | 每个活动二分器四个位置 |
| 修改顶点调度 | 增量 LEB 几何更新 | 本轮修改二分器数 |

`indirectDrawBuffer[0..3]` 同时构成 `D3D12_DRAW_ARGUMENTS`，用于程序化间接绘制；`indirectDrawBuffer[9]` 保存显式活动二分器数量。

## 9. 几何生成与渲染路径

### 9.1 逻辑几何生成

[`PlanetGeometry.compute`](../../third_party/large_cbt/shaders/PlanetGeometry.compute) 通过 `heapID` 解码最长边二分路径，从基础三角形计算当前二分器的三个子顶点和一个父级辅助顶点。

输出采用行星坐标：先在基础多面体上求值，再归一化到球面半径。若设备支持双精度，逻辑位置缓冲使用 `double3`，否则退化为 `float3`。

### 9.2 形变

水体和月球形变着色器读取活动二分器列表，将逻辑位置转换为相机相对位置，再采样水波或高程纹理，生成：

- `currentVertexBuffer`：最终绘制顶点；
- `currentDisplacementBuffer`：材质和法线重建所需的位移。

### 9.3 程序化间接绘制

官方实现不生成传统索引缓冲。顶点着色器使用 `SV_VertexID / 3` 得到活动三角形序号，再通过 `indexedBisectorBuffer` 找到真实物理槽位，最后读取该槽位对应的三个顶点。

这条路径使用 `D3D12_INDIRECT_ARGUMENT_TYPE_DRAW`，而当前 RoamTesting 的 GPU 路径使用 `D3D12_INDIRECT_ARGUMENT_TYPE_DRAW_INDEXED`。这是桥接时最重要的接口差异之一。

## 10. 并发与正确性机制

### 10.1 并发原语

核心并发机制包括：

- 原子计数器追加候选和传播任务；
- 原子扣减剩余容量；
- 原子设置 `subdivisionPattern`，使首个成功线程拥有该节点；
- 64 位 CBT 位域原子置位和清位；
- 组共享内存加速压缩求和树访问；
- 每个阶段后的 UAV 屏障；
- 邻接双缓冲避免大范围原地覆盖。

### 10.2 正确性边界

官方实现提供 `Validate` 阶段，主要验证活动二分器的邻接是否对称，即当前节点指向邻居时，邻居也必须在三个邻接槽位之一指回当前节点。

它能发现邻接破坏，但不能证明：

- 候选分配满足全局最优；
- 相同输入在不同 GPU 调度下产生完全相同的候选获批集合；
- 所有视觉误差指标达到某个全局上界。

### 10.3 同步实现的工程限制

官方演示在每帧呈现后调用命令队列 `flush`，随后处理计时、验证和占用读回。这使演示结构简单，但会串行化 CPU 与 GPU，不适合作为我们的最终性能架构。

接入 RoamTesting 时应保留拓扑阶段的 GPU 内部屏障语义，但使用现有双帧资源和围栏延迟读回，不复制每帧全队列等待。

## 11. 当前预算策略的源码结论

源码能够直接支持此前的研究判断：CBT 2024 解决了固定内存池中的并发拓扑维护，但没有执行全局预算优化。

具体表现为：

1. `Classify` 只输出离散状态，没有连续收益分数；
2. split 候选以原子追加顺序进入数组；
3. `SplitElement` 按单个候选保守估计最大兼容链成本；
4. 容量不足时恢复计数并立即放弃；
5. 不存在候选排序、收益/成本比较、共享闭包去重或预算交换；
6. 哪些候选先成功预留取决于并行执行顺序。

代码中虽然有“高优先级”注释，但当前实现仍只返回统一的 `BISECT_ELEMENT`，没有独立优先级字段或第二条高优先级队列。

因此，固定容量下的结果是“拓扑合法且不越界”，不是“把容量分配给全局收益最大的候选”。

## 12. 与 RoamTesting 的架构映射

| large_cbt | RoamTesting 对应位置 | 处理建议 |
|---|---|---|
| `SpaceRenderer` | `Application`、`D3D12GraphicsBackend`、`TerrainRenderer` | 不迁移，沿用现有主循环和后端 |
| 自研 DX12 backend | `D3D12GraphicsBackend` | 不迁移，仅补足缺失的程序化间接绘制和资源接口 |
| `Planet` | `TerrainRenderer` + `ITerrainLodAlgorithm` 实例 | 拆分拓扑算法与几何/渲染职责 |
| `MeshUpdater` | 新的 CBT 算法内部命令编排器 | 迁移核心阶段和屏障顺序 |
| `CBTMesh` | CBT 算法持有的 GPU 常驻状态 | 作为算法私有状态，不暴露所有权 |
| `GPU_CBT` | CBT 占用树资源 | 原样保持双缓冲布局和容量特化语义 |
| `UpdateMesh.compute` | `assets/shaders/dx12/cbt/` | 先忠实迁移，后续研究另建调度变体 |
| `PlanetGeometry.compute` | 高度图几何求值着色器 | 不直接照搬球面逻辑，替换为地形基网格求值 |
| 水体/月球形变 | 高度图采样和法线生成 | 不迁移演示效果 |
| 程序化间接绘制 | 当前仅支持索引间接绘制 | 增加 CBT 专用程序化绘制数据包或渲染模式 |

### 12.1 建议的算法边界

新增实现应遵循现有统一接口：

```text
CbtTerrainLodAlgorithm : ITerrainLodAlgorithm
├── CbtGpuState                 持有 CBT、heapID、邻接、任务和索引缓冲
├── CbtTopologyPipeline         记录 Classify 到 Reduce 的命令
├── CbtGeometryEvaluator        将 heapID 转换为高度图地形顶点
└── TerrainLodRenderPacket      借用 GPU 资源，不转移所有权
```

其中 `CbtGpuState` 必须跨帧持有，不能像当前 GPU ROAM-like 的过渡实现一样依赖 CPU 数据导向拓扑快照重建完整状态。

### 12.2 渲染数据包需要补足的语义

当前 [`TerrainLodRenderPacket`](../../src/algorithms/ITerrainLodAlgorithm.h) 假设 GPU 间接路径提供顶点缓冲、索引缓冲和 `DRAW_INDEXED` 参数。忠实 CBT 复现更适合增加程序化间接模式，至少携带：

- 最终顶点缓冲；
- 活动二分器索引缓冲；
- `D3D12_DRAW_ARGUMENTS` 间接命令缓冲；
- 活动三角形数和资源生命周期代次。

也可以额外生成传统索引缓冲以适配现有 renderer，但这会增加官方实现不存在的 mesh emit 阶段，只适合作为适配实验，不应充当忠实复现基线。

### 12.3 参数映射

| 官方参数 | RoamTesting 参数 | 说明 |
|---|---|---|
| `_TriangleSize` | 新增或重新定义的屏幕面积阈值 | 官方单位是像素面积，不等同于当前距离误差阈值 |
| `_MaxSubdivisionDepth` | `MaxDepth` | 可以直接映射，但需受 64 位 `heapID` 深度限制 |
| CBT 类型 | 新增 `CbtCapacity` | 建议支持 128K、256K、512K、1M |
| 视图投影与屏幕大小 | 现有 `RenderContext` | 应进入算法输入或 GPU 常量缓冲 |
| 基础网格 | 当前高度图根三角形/规则网格 | 需要显式半边邻接和基础 `heapID` 构建 |

当前 `TerrainLodBuildInput` 只有相机位置，没有视图投影矩阵、视锥和平面尺寸。CBT 面积分类需要扩展算法输入，或由后端提供只读帧上下文；不建议在算法中自行重建与 renderer 不一致的相机矩阵。

### 12.4 建议新增统计字段

为复现和后续预算论文，至少记录：

- CBT 容量、活动位数量和剩余槽位；
- split 与 simplify 候选数；
- 成功预留、容量拒绝和重复认领数量；
- 实际分配槽位数与保守预留返还数；
- 兼容链遍历步数和最大长度；
- merge 提交数和释放槽位数；
- split、allocate、bisect、propagate、simplify、reduce、indexation 各阶段 GPU 时间；
- 活动、可见和本轮修改二分器数量；
- 邻接验证错误数；
- 普通帧 CPU/GPU 围栏等待时间。

## 13. 推荐复现顺序

### 阶段 A：冻结官方参考程序

- 在 `third_party/large_cbt` 独立生成并运行 `outer_space`；
- 固定提交、适配器、驱动、分辨率、CBT 类型和相机路径；
- 记录官方界面中的占用量、各阶段时间和截图；
- 不修改算法着色器。

### 阶段 B：桥接官方拓扑，不改算法语义

- 在 RoamTesting 中增加 CBT 算法实例和 GPU 常驻状态；
- 迁移 OCBT、`UpdateMesh.compute` 和命令阶段顺序；
- 先使用简单固定基网格验证 split、merge、邻接和占用量；
- 增加程序化间接绘制路径；
- 对照官方相同输入的活动位和拓扑验证结果。

### 阶段 C：适配高度图地形

- 为规则地形构建半边基础网格；
- 用高度图求值替换球面 `PlanetGeometry.compute`；
- 保持 CBT 拓扑、分配和传播阶段不变；
- 接入现有相机路径、界面和 benchmark。

### 阶段 D：建立论文基线

- 冻结“官方先到先分配”策略；
- 增加容量饱和场景和候选超过预算的测试；
- 收集候选收益、兼容链成本、拒绝原因和预算利用率；
- 在独立着色器变体中实现全局预算调度，避免污染复现基线。

## 14. 已知风险与待确认问题

1. **许可证缺失。** 在使用或发布衍生代码前需要联系作者或确认授权。
2. **程序化绘制接口不匹配。** 当前 renderer 的 `DRAW_INDEXED` 契约需要扩展。
3. **相机输入不足。** 当前算法接口缺少视图投影、视锥和屏幕尺寸。
4. **基础网格不同。** 官方以一般半边网格和行星几何为目标，地形适配必须重新生成基础邻接。
5. **每帧全队列等待。** 官方演示的 `flush` 不可直接作为性能实现迁移。
6. **容量静态特化。** 四种 OCBT 规模对应不同 HLSL 布局，运行时切换需要重新创建资源和管线。
7. **Shader Model 6.6。** 64 位位域原子操作和官方 DX12 路径需要在设备初始化阶段显式检查。
8. **调度非确定性。** 容量饱和时获批候选可能随 GPU 执行顺序变化，应建立重复运行稳定性实验。
9. **验证覆盖有限。** 官方验证主要覆盖邻接对称性，需要补充位域、heapID、活动列表和间接命令一致性检查。

## 15. 关键源码索引

| 主题 | 源码 |
|---|---|
| 论文与构建说明 | [`README.md`](../../third_party/large_cbt/README.md) |
| 应用与每帧编排 | [`space_renderer.cpp`](../../third_party/large_cbt/demo/src/render_pipeline/space_renderer.cpp) |
| 单个自适应网格实例 | [`planet.cpp`](../../third_party/large_cbt/demo/src/render_pipeline/planet.cpp) |
| CPU 基网格和半边展开 | [`cpu_mesh.cpp`](../../third_party/large_cbt/demo/src/mesh/cpu_mesh.cpp) |
| GPU 资源布局 | [`mesh.h`](../../third_party/large_cbt/demo/include/mesh/mesh.h)、[`mesh.cpp`](../../third_party/large_cbt/demo/src/mesh/mesh.cpp) |
| 拓扑阶段命令编排 | [`mesh_updater.cpp`](../../third_party/large_cbt/demo/src/mesh/mesh_updater.cpp) |
| 分类与计算入口 | [`UpdateMesh.compute`](../../third_party/large_cbt/shaders/UpdateMesh.compute) |
| split、merge、传播与验证 | [`update_utilities.hlsl`](../../third_party/large_cbt/shaders/shader_lib/update_utilities.hlsl) |
| OCBT 通用操作 | [`ocbt_generic.hlsl`](../../third_party/large_cbt/shaders/shader_lib/ocbt_generic.hlsl) |
| 128K OCBT 布局示例 | [`ocbt_128k.hlsl`](../../third_party/large_cbt/shaders/shader_lib/ocbt_128k.hlsl) |
| 逻辑二分几何求值 | [`PlanetGeometry.compute`](../../third_party/large_cbt/shaders/PlanetGeometry.compute) |
| 程序化间接绘制 | [`VisibilityPass.graphics`](../../third_party/large_cbt/shaders/Visibility/VisibilityPass.graphics) |

## 16. 结论

`large_cbt` 最值得迁移的不是其完整演示渲染器，而是四项相互配合的核心能力：

1. 物理槽位与逻辑二分路径分离的常驻拓扑；
2. 支持已占用/空闲 rank-select 的 OCBT 内存池；
3. split、merge 和邻接传播的多阶段并行提交协议；
4. 活动索引、间接调度和程序化间接绘制形成的 GPU 驱动闭环。

对 RoamTesting 而言，最合理的做法是保留现有 DX12 后端、界面和 benchmark，只迁移上述拓扑闭环，并为官方的程序化绘制和相机分类输入补足最小接口。完成忠实复现后，再在候选分类与内存预留之间插入预算调度阶段，才能保证论文改进与官方基线具有清晰、可解释的差异。
