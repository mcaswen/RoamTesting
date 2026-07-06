# Milestones

核心原则：每一步都应有一个可运行、可截图、可录制、可验证的阶段成果。

## 阶段 0：工程初始化与最小渲染闭环

### 目标

搭建 SDL2 + CMake + OpenGL 基础工程，确保开发环境稳定。

### 任务

- 配置 CMake；
- SDL2 创建窗口与 OpenGL Context；
- 接入 GLAD/GLEW；
- 接入 GLM；
- 接入 Dear ImGui；
- 实现 `Application` 主循环；
- 实现 FPS 相机、鼠标视角、WASD 移动；
- Shader 编译、日志输出和错误处理；
- 创建基础地面三角形，验证渲染链路。

### 验收标准

- 程序可编译、可启动；
- 可自由移动相机；
- 可显示三角形或简单平面；
- ImGui 面板可显示 FPS 与窗口尺寸。

### 当前实现状态（2026-07-03）

- 已完成 SDL2 窗口、OpenGL Context、GLAD 加载、GLM 矩阵、Dear ImGui 调试面板和最小渲染闭环；
- 已建立 `Application`、`platform::Window`、`render::Shader`、`render::TriangleRenderer`、`gui::ImGuiLayer`、`CameraController` 等基础模块；
- `debug-fetch` preset 会拉取/接入完整依赖并构建可交互应用；
- 默认 `debug` preset 在依赖不完整时保持 bootstrap 模式，用于验证基础配置，不强制联网拉取全部库；
- 当前渲染内容是一个地面三角形占位，用于验证相机、shader、深度测试和 UI overlay，阶段 1 再替换为 height map terrain mesh。

### 运行方式

```bash
cmake --preset debug-fetch
cmake --build --preset debug-fetch
./build/debug-fetch/bin/ParallelROAM
```

自动启动验证可使用 `./build/debug-fetch/bin/ParallelROAM --smoke-test`，程序会渲染数帧后退出。

交互方式：`W/A/S/D` 移动，`Space/Ctrl` 升降，按住鼠标右键移动视角，`Shift` 加速，`Esc` 退出。

## 阶段 1：地形显示 + 基础 UI

### 目标

先实现“没有 ROAM 的标准 Height Map 地形”，建立可视化与渲染基线。

### 任务

- 加载灰度 Height Map；
- 创建规则网格 terrain mesh；
- 根据 Height Map 采样高度；
- 计算基础法线；
- 实现 Phong 或 Blinn-Phong 光照；
- 加载简单地表纹理；
- 支持 wireframe；
- 支持 terrain size、height scale 调整；
- UI 显示 FPS、Draw Call、顶点数 / 三角形数、Height Scale、光照参数和 wireframe 开关。

### 验收标准

- 可从多个角度观察起伏地形；
- 地形无明显法线错误；
- 线框与实体模式均正确；
- Height Map 与世界坐标映射明确；
- 可截图作为报告“基础渲染模块”证据。

### 当前实现状态（2026-07-05）

- 已完成基于 Height Map 的规则网格 terrain baseline；
- `HeightMap` 使用 stb 读取灰度图片，并统一归一化到 `0..1`；
- `TerrainMeshBuilder` 根据 Height Map 生成规则网格顶点、索引、UV 和基础法线；
- `TerrainRenderer` 负责 OpenGL VAO/VBO/EBO、地表纹理、Blinn-Phong 光照和 wireframe；
- ImGui 面板显示 FPS、窗口尺寸、Height Map 尺寸、顶点数、三角形数、Draw Call，并支持调整 terrain size、height scale、wireframe 和光照参数；
- 当前仍是规则网格渲染，不包含 ROAM split/merge，自适应 LOD 从阶段 2 开始实现。

### 默认资源

```text
assets/heightmaps/Hm_Terrain_Test_129.pgm
assets/textures/Tex_Terrain_Debug_Diffuse.ppm
```

两个资源均为 stb 可加载的二进制 PNM 测试资源。后续可替换为同路径或代码中指定的 `png/jpg/tga/pgm/ppm` 资源。

## 阶段 2：基础 ROAM（Classic CPU 版）

### 目标

先完成可运行的 Classic CPU ROAM 原型，再补完符合经典 ROAM 语义的局部拓扑维护、diamond split、diamond merge 和裂缝约束。阶段 2 必须成为后续 Data-Oriented CPU 版和 GPU 版的可信 baseline。

### 子阶段

2A：二维二叉三角树可视化

- 不加载复杂高度；
- 使用平面或简单函数高度；
- 两个根三角形；
- 支持手动点击/按键 split；
- 显示每个节点 depth；
- 验证 child 的 domain triangle 正确。

2B：按距离细分

- 根据相机距离决定 split；
- 暂时仅 split，不 merge；
- 遍历 leaf triangle 并渲染；
- 观察近处细、远处粗。

2C：Height Map 细分

- 新顶点中点从 Height Map 采样；
- 将二维 domain triangle 转为三维 render triangle；
- 实现简单 geometric error；
- 使地形崎岖区域优先细分。

2D：邻接关系与裂缝处理

- Classic 节点使用裸指针表达 `parent`、`leftChild/rightChild`、`baseNeighbor/leftNeighbor/rightNeighbor`；
- split 前递归处理 `baseNeighbor`，保证当前 triangle 和 base neighbor 能组成合法 diamond；
- split 后立即按经典 ROAM 规则连接 child 的 `base/left/right neighbor`；
- split 后同步更新相邻 triangle 对当前节点的反向 neighbor 引用；
- 运行时路径不依赖全局 T-junction 扫描修裂缝；
- invariant checker 可离线扫描 active leaf，验证没有 T-junction、邻接互反关系正确；
- UI 提供 neighbor / diamond 传播统计，便于观察 forced split 成本。

2E：merge 与 hysteresis

- 引入 `splitThreshold > mergeThreshold`；
- merge 只能按完整 diamond 成对执行，不能只 merge 单侧 sibling；
- merge 前确认两个 sibling 都是 leaf，且对侧 diamond child 也满足回收约束；
- merge 后恢复父节点和周围 neighbor 的互相引用；
- 误差位于 split / merge 阈值之间时保持当前拓扑，防止频繁抖动；
- 记录 splitCount / forcedSplitCount / mergeCount / rejectedMergeCount。

### 验收标准

- 近处和地形变化大的区域明显细分；
- 远处保持粗网格；
- wireframe 可清晰展示 LOD；
- 默认运行路径不依赖全局 repair pass；
- invariant checker 验证无 T-junction；
- neighbor 指针满足互反关系和 diamond 约束；
- merge 不破坏裂缝约束；
- 能输出 active triangle count；
- 可作为三版本视觉一致性的 baseline。

### 当前实现状态（2026-07-05）

- 已新增 `algorithms/classic_roam/ClassicRoamMeshBuilder`，实现 Classic CPU ROAM 的可运行原型；
- Classic 节点已经采用裸指针结构，包含 parent、child 和 base/left/right neighbor 指针；
- 已使用两个根三角形覆盖完整 Height Map domain，并以二叉三角树方式沿 base edge 递归 split；
- 当前 split 决策基于边中点和重心最大几何误差、近距投影边长、相机距离、`SplitThreshold`、`MergeThreshold` 和 `DistanceScale`；
- `PathId` 已按两棵 root tree 分区，避免 hysteresis 和 merge 统计发生路径碰撞；
- 新生成的 split 顶点会从 Height Map 双线性采样高度，并用 Height Map 梯度估算法线；
- ROAM 输出三角绕序会统一修正到正 Y 方向，与规则网格 baseline 保持一致；
- 已接入一部分基于 `baseNeighbor` 的 forced split 传播，右侧面板显示强制 split、约束传播次数和裂缝风险；
- 当前 T-junction 主要依赖全局 repair pass 兜底，这不是完整经典 ROAM 的正确实现方式；
- 当前 merge / hysteresis 已改为基于持久化拓扑和严格 diamond merge 的基础实现；
- `TerrainRenderer` 支持在规则网格 baseline 和 Classic ROAM mesh 之间切换；
- Classic ROAM rebuild 已加入相机位移阈值缓存，避免静止或微小移动时每帧重建和上传 mesh；
- 默认交互路径已移除全局 T-junction repair，不再依赖 `O(L^2)` repair pass 修裂缝；
- 已新增局部 baseNeighbor 约束，split 前会追踪到互为 base 的合法 diamond；
- 已新增 priority queue split candidate 策略，避免纯递归遍历顺序影响细分分布；
- 已新增拓扑验证开关，开启后可统计 T-junction、邻接错误和 validate 耗时；
- 已将 Classic ROAM builder 改为持久化拓扑，不再每次 build 都清空整棵树；
- 已新增严格 diamond merge，只有 sibling leaf 和互为 base 的 diamond 满足条件时才回收；
- merge 会恢复 parent 的 left/right neighbor，并保持 base neighbor 互指；
- 右侧 ImGui 面板已加入 Classic ROAM 开关、局部约束开关、拓扑验证开关、节点数、split/merge 统计、实际深度、最大深度、split/merge 阈值、候选队列峰值、merge 拒绝和阶段耗时；
- 当前覆盖 2A、2B、2C，并已推进 2F、2G、2H、2I、2J、2K、2L 的基础实现；
- 阶段 2 尚未完全封版，后续重点是更完整的 debug draw、固定测试入口和报告用可视化。

### 完整 Classic ROAM 补完计划

2F：关闭默认全局 repair

- 将全局 T-junction repair pass 从默认交互路径移除；
- 保留 invariant checker 作为 debug / test 工具，不参与每帧修复；
- UI 中将“裂缝修复”改为“经典局部约束”或类似名称，避免误导为全局 repair；
- 性能面板记录 update ms、split ms、merge ms、emit ms。

2G：建立经典拓扑不变量

- 每个节点明确保存 parent、leftChild、rightChild、baseNeighbor、leftNeighbor、rightNeighbor；
- root diamond 初始化后必须满足两个根互为 base neighbor；
- leaf split 前后都要保持 neighbor 指针互反；
- 内部节点不参与渲染 leaf 输出，但要保留 child 和 diamond 关系；
- 添加 `ValidateTopology()`，检查 dangling pointer、非互反 neighbor、非法 child 和 T-junction。

2H：完整 diamond split

- `SplitTriangle(node)` 只处理 leaf；
- 若 `node->BaseNeighbor` 存在且无法与当前节点直接组成 diamond，先递归 split base neighbor；
- 当前节点和 base neighbor 都满足 diamond 条件后再分裂；
- 分裂后按经典规则连接四个 child 的 neighbor；
- 分裂后更新左邻、右邻和 base neighbor child 对当前 child 的反向引用；
- forced split 只沿局部 neighbor 链传播，不扫描全局 leaf 集合。

2I：误差队列与 split 策略

- 预计算或缓存 geometric variance，避免每帧重复高成本采样；
- 使用 screen-space error 计算当前 split priority；
- 使用 max heap 管理 split candidate；
- 使用最大深度和 split / merge 阈值限制细分规模；
- 记录 split queue size、实际 split 次数和 forced split 次数。

2J：完整 diamond merge

- 已使用候选列表管理 merge candidate；
- 已限制 merge 只能回收 sibling leaf，不能只回收单侧 child；
- 若 base neighbor 也 split，则必须互为 base 并且两侧 child 都是 leaf，才允许成对 merge；
- merge 后会把外部 neighbor 指回 parent，并保持 diamond parent 互为 base neighbor；
- hysteresis 通过持久化拓扑、split / merge 双阈值和当前拓扑保持实现，不再只依赖路径 ID 假装 merge。

2K：验证与调试可视化

- 已增加拓扑验证开关，输出 active leaf 数、T-junction 数、非法 neighbor 数、最大深度和 validate 耗时；
- 已通过临时 probe 对比规则网格和 Classic ROAM 的高度范围、三角绕序、坐标范围、退化三角形和索引越界；
- wireframe 模式用于观察 Classic ROAM 细分结果；
- 尚未完成正式 debug draw，后续补按 depth 着色、forced split 高亮和 diamond 对高亮；
- 每次修改 topology 后必须运行 smoke test 和 topology validator。

2L：阶段 2 完成标准

- 默认交互路径已无全局 `O(L^2)` repair；
- 开启 Classic ROAM 后，近处细分和远处 merge 会随相机位置变化；
- validator 在临时 probe 的近处 / 远处切换中报告 T-junction 为 0、invalid neighbor 为 0；
- split / merge 已具备局部 diamond 约束，但还需要更长时间交互验证；
- 当前输出已经可作为阶段 3 DOD 重构的第一版行为标准。

## 阶段 3：Data-Oriented CPU 版本

### 目标

在相同误差阈值、Height Map 和相机路径下，以数据导向方式重构 ROAM，验证 CPU 多核与数据布局收益。

### 子阶段

3A：指针树转 Index-Based Node Pool

- 将 child / neighbor 从指针改为 index；
- 使用预分配 node pool；
- 避免运行期频繁 new/delete；
- 保留与 Classic 版一致的 split 语义。

当前状态：

- 已新增 `DataOrientedRoamMeshBuilder`，节点池由 `std::vector` 预分配管理，parent / child / neighbor 统一使用 `NodeIndex`；
- 已通过统一 `ITerrainLodAlgorithm` 接口接入 benchmark，`--algorithm dod` 可运行 index-based 3A 版本；
- 3A 已作为 index-based baseline 保留在提交历史中，当前主线已进入 3B SoA 节点池实现。

3B：AoS 转 SoA

- 将 error、depth、flags、neighbor indices 分离；
- 只在需要时读取对应数组；
- 对齐 / padding 进行基本检查；
- 保证结果与 Classic 版一致或可解释地接近。

当前状态：

- `DataOrientedRoamNodePool` 已改为 SoA 数组，domain、parent/child、neighbor、error、depth、build id 和 flag 分离存储；
- `ScreenErrors` 缓存最近一次 split / merge 队列评分，为 3C 并行误差评估保留连续写入目标；
- DOD 私有统计记录 SoA 数组数量和容量估算，统一 benchmark 接口保持不变。

3C：线程池与并行误差评估

- 实现轻量线程池或使用 EnTT/task scheduler；
- `ErrorEvaluationSystem` 并行；
- 记录单线程与多线程耗时；
- 每阶段记录 CPU 时间。

当前状态：

- 新增 DOD `ErrorEvaluation` pass，先收集当前 active leaf，再批量刷新 SoA `ScreenErrors`；
- 自动 worker 模式会按硬件线程数保守封顶，小批量 leaf 保持串行以避免线程启动成本吞掉收益；
- 统一 benchmark 的 `CpuErrorEvalMilliseconds` 已接入 DOD 批量误差评估耗时，`CpuDecisionMilliseconds` 记录扣除该批量评估后的 split 决策时间；
- 统一 UI 和 benchmark 已输出 CPU worker 数与 CPU 占用率，用于观察并行评估是否真正吃到多核；
- 拓扑提交、约束传播和 split / merge 仍保持单线程，为 3D 的并行候选标记与 thread-local 收集保留清晰边界。

3D：并行标记与收集

- 并行标记 split / merge candidate；
- thread-local buffer 收集 active leaves；
- 合并结果；
- 避免共享 vector 锁竞争。

当前状态：

- DOD split pass 已改为按 node index 分块扫描 active topology，并用 thread-local buffer 收集 active leaves；
- split candidate 标记读取批量 `ScreenErrors` 缓存并行过滤，合并后统一分配稳定 sequence 再进入 priority queue；
- merge candidate 标记并行扫描 active internal node，真正 diamond merge 和拓扑提交仍保持串行；
- `CpuCollectMilliseconds` 已汇总 active leaf 收集、split candidate 标记和 merge candidate 标记耗时。

3E：拓扑提交策略

- 初版保留单线程 topology commit；
- 可选：按 Terrain Chunk 分区处理；
- 可选：边界采用 skirt 或边界约束，降低跨 chunk 同步复杂度。

当前状态：

- DOD topology commit 已加入固定 `8x8` terrain chunk 分区，只有完整落在同一 chunk 的候选会进入并发提交；
- split 并发提交只处理已有 child 可复用、且不会触发 forced split 的内部候选，fresh child 分配和跨 chunk 邻接继续串行回退；
- merge 并发提交只处理影响节点全集都在同一 chunk 内的候选，diamond merge 跨 chunk 时仍由串行路径保证 neighbor 一致性；
- 统一 `CpuWorkerCount` 已纳入 topology commit worker 数，`CpuTopologyMilliseconds` 继续覆盖并发 batch 与串行回退的总拓扑提交耗时。

### 验收标准

- 视觉结果与 Classic 版相同或接近；
- 在中等/大规模场景下更新耗时优于 Classic 版；
- 有可展示的阶段时间分解；
- 能说明哪些环节并行收益高、哪些环节被拓扑依赖限制。

## 阶段 4：GPU ROAM-like 版本

### 目标

在不破坏 Classic / DOD 结果可比性的前提下，把最适合 GPU 批处理的 ROAM 阶段逐步迁移到 GPU。GPU 版优先证明“误差评估、候选标记、active leaf 收集、mesh emit / draw submit”这些高并行环节的收益，拓扑 split / merge 作为后续冲刺，不阻塞主报告。

GPU 版不强求原样复刻 Classic 的全局优先队列。推荐项目表述为：保留二叉三角域、视点相关 screen error、split / merge 阈值和无裂缝约束思想，用 GPU-friendly 的批量阈值决策与分块提交替代严格串行队列。

### 现有前置条件（2026-07-06）

- 已有统一 `ITerrainLodAlgorithm`、`TerrainLodRenderPacket` 和 `TerrainLodStats`，枚举中已预留 `GpuRoamLike`；
- `TerrainLodRenderPacket` 已预留 `GpuBuffers`、`GpuIndirect`、GPU buffer id 和 indirect draw buffer 字段；
- 当前 `TerrainRenderer` 仍只消费 CPU mesh，GPU-only packet 分支尚未实现；
- Classic 与 DOD 已在 smoke benchmark 中保持相同三角形数和拓扑统计，可作为 GPU 版行为对照；
- DOD 已具备 SoA node pool、active leaf 快照、chunk id 缓存、并行 candidate marking 和保守 chunk topology commit，是 GPU buffer schema 的主要参考；
- Benchmark CSV 已包含 `gpuComputeMs`、`cpuGpuUploadBytes`、`cpuGpuReadbackBytes`、CPU worker 和 CPU 利用率字段；
- 当前 macOS 测试环境 OpenGL 运行时报告为 4.1，Compute Shader 需要 OpenGL 4.3 或对应扩展，因此必须先实现 GPU capability gate，无法运行 compute 时 benchmark 应明确 skip 而不是失败。

### 分级交付

```text
Level A：GPU adapter + buffer schema + capability gate
Level B：GPU error evaluation + candidate marking，CPU topology commit
Level C：GPU active leaf compaction + GPU mesh emit / GPU buffer rendering
Level D：GPU indirect draw，尽量减少 CPU readback
Level E：GPU split-only 或 split/merge topology update
```

主线目标建议定为 Level C；Level D 是强展示项；Level E 是冲刺项。若目标机器没有 compute shader 能力，仍应完成 Level A，并让 benchmark / UI 能清晰说明 GPU 路径不可用原因。

### 子阶段

4A：GPU 能力检测与算法壳

- 新增 `GpuRoamLike` 算法适配器，接入统一 `ITerrainLodAlgorithm`；
- 在 renderer / benchmark 中允许选择 GPU 版，但当 OpenGL compute 不可用时返回明确 skip / error message；
- 抽出 GPU capability 查询，记录 OpenGL version、compute shader、SSBO、atomic counter、indirect draw、timer query 支持情况；
- 建立 GPU shader / buffer 资源生命周期规范，避免算法层直接散落 OpenGL 对象管理；
- 接入 OpenGL timer query 包装，统一写入 `GpuComputeMilliseconds`；
- 暂不改变地形输出，目标是让三版本入口和失败语义稳定。

验收标准：

- `--algorithm all` 在 GPU 不可用时稳定跳过 GPU，不影响 Classic / DOD benchmark；
- UI 可以显示 GPU 路径不可用原因；
- `GpuRoamLike` 的 `Info()`、`Capabilities()`、`Stats()` 和 `Reset()` 路径完整；
- 不支持 compute 的机器上也能通过 smoke test。

当前实现记录：

- 已新增 `GpuRoamLike` 算法壳并接入 renderer、UI 下拉框和 CLI benchmark；
- 已新增 OpenGL GPU capability 查询，记录 context、OpenGL version、renderer、compute shader、SSBO、atomic counter、indirect draw 和 timer query 能力；
- 无窗口 benchmark 没有 OpenGL context 时，`--algorithm all` 会把 GPU 明确标为 skip，`--algorithm gpu` 会返回失败并输出原因；
- macOS OpenGL 4.1 环境下，UI 选择 GPU ROAM-like 时会显示 OpenGL 4.3 / compute shader 不可用原因，不会静默回退成 CPU 成绩；
- 运行时 benchmark CSV 和汇总表已预留 `gpuComputeMs`、CPU-GPU upload bytes 和 readback bytes；
- 当前 GPU 版仍处于 Level A，尚未执行 compute shader、GPU mesh emit 或 GPU buffer rendering。

4B：GPU buffer schema 与 DOD 快照对齐

- 以 DOD SoA 字段为基准定义 GPU node buffer 布局；
- 明确 std430 对齐规则，避免 CPU struct padding 和 shader 端读取不一致；
- 将 domain、parent / child、neighbor、depth、error、screenError、flags、pathId、chunkId 拆成 GPU-friendly buffer；
- 上传 Height Map texture、camera/settings UBO 和 active leaf index buffer；
- 增加 `CpuGpuUploadBytes` 统计，区分 full upload 与 dirty range upload；
- 先只上传数据，不在 GPU 上修改拓扑。

验收标准：

- GPU buffer 中 node 数、active leaf 数、max depth 与 DOD stats 对齐；
- 上传字节数进入 benchmark CSV；
- 支持 debug readback 少量 node 做字段一致性抽样；
- 不引入额外 UI 参数，沿用三版本统一核心参数。

4C：GPU Error Evaluation

- Compute shader 读取 height map texture、node buffer、camera/settings UBO；
- 对 active leaf 计算与 DOD `ComputeScreenErrorScore` 对齐的 screen error；
- 输出 `screenError` buffer；
- CPU 只抽样 readback 少量 error 值做验证，默认 benchmark 不全量 readback；
- 用 timer query 记录 compute 时间，用 CPU 计时记录 upload / readback 时间。

验收标准：

- 抽样 GPU error 与 DOD CPU error 的误差在可解释范围内；
- CSV 同时记录 `CpuErrorEvalMilliseconds`、`GpuComputeMilliseconds`、upload/readback bytes；
- GPU 不可用时该阶段保持 skip，不破坏 CPU benchmark；
- 文档记录浮点误差、采样方式和 OpenGL 版本要求。

4D：GPU Candidate Marking 与候选压缩

- Compute shader 根据 `screenError`、split / merge 阈值、depth 和 active 状态写 split / merge flag；
- 使用 atomic append 或 prefix-sum compact 输出 split candidate list、merge candidate list；
- CPU topology commit 暂时继续复用 DOD 保守提交策略，GPU 只负责大批量标记；
- readback 只读取 compact 后候选列表和计数，不读取全量 flag；
- 对比 DOD candidate marking 的 CPU collect / mark 耗时。

验收标准：

- GPU 候选数量与 DOD 在 smoke profile 下保持一致或差异可解释；
- CPU readback bytes 明确低于全量 node buffer；
- topology commit 后 active triangle count 与 Classic / DOD 对齐；
- benchmark 可展示 CPU collect / mark 时间下降，或说明瓶颈转移到 readback / topology commit。

4E：GPU Active Leaf Compaction

- GPU 遍历 node buffer，压缩 active leaf index；
- active leaf buffer 成为 error evaluation、candidate marking 和 mesh emit 的共享输入；
- 默认只 readback active leaf count 和必要统计；
- 继续用 CPU topology commit，保证风险集中在收集和数据流上。

验收标准：

- active leaf count 与 DOD 最终 leaf 快照一致；
- `CpuCollectMilliseconds` 相比 DOD 路径下降或被明确替换为 GPU compute；
- readback 口径清楚，只读计数时不影响主要性能结论；
- debug 模式可选择全量 readback 以定位错误，但 benchmark 默认关闭。

4F：GPU Mesh Emit 与 `GpuBuffers` 渲染分支

- Compute shader 根据 active leaf buffer 生成 GPU vertex / index buffer；
- `TerrainRenderer` 支持消费 `TerrainLodRenderMode::GpuBuffers`，不再要求 `CpuMesh` 非空；
- 顶点 position、normal、uv、debug color 与 CPU emit 对齐；
- 保留 CPU mesh fallback，便于 GPU 输出错误时快速回退对照；
- 统计 `CpuMeshBuildMilliseconds`、`CpuUploadMilliseconds` 和 `CpuGpuUploadBytes` 的下降。

验收标准：

- GPU buffer 渲染画面与 DOD CPU mesh 视觉一致；
- smoke benchmark 中 active triangle count 与 CPU 版本一致；
- CPU mesh build 和 CPU-GPU mesh upload 在 GPU 路径中接近 0 或只剩 fallback/debug 成本；
- renderer 对 `CpuMesh` 为空但 GPU buffer 有效的 packet 不再报错。

4G：GPU Indirect Draw

- 在 GPU 端生成 indirect draw command；
- `TerrainRenderer` 支持 `TerrainLodRenderMode::GpuIndirect`；
- CPU 只提交一次 indirect draw，不读取完整 index count；
- 保留 timer query，区分 GPU compute 和 render draw 时间。

验收标准：

- `DrawElementsIndirect` 或等价路径可以绘制 active leaf mesh；
- CPU readback 只保留统计所需最小数据；
- `RenderMilliseconds` 与 `GpuComputeMilliseconds` 分开记录；
- GPU unavailable 或 indirect draw unsupported 时可回退 `GpuBuffers`。

4H：GPU Split-Only Topology Update（冲刺）

- 在 GPU 端从 split candidate 批量提交 split-only；
- 采用固定容量 node pool 和 atomic allocation，暂不回收节点；
- 只处理 chunk interior candidate，跨 chunk / forced split / fresh dependency 先回退 CPU 或延后；
- 使用多轮 compute pass 传播必要的局部约束，限制最大迭代次数；
- CPU validator 可通过 debug readback 验证 T-junction、neighbor 和 max depth。

验收标准：

- split-only 场景下 active triangle count 能随相机接近增加；
- 不支持 merge 时必须在 UI / benchmark 中明确标注能力边界；
- validator 在固定 smoke 场景中无 T-junction 和 invalid neighbor；
- 若出现约束无法收敛，必须记录失败案例和回退策略。

4I：GPU Split / Merge Topology Update（可选冲刺）

- 在 GPU 端增加 merge candidate 和 sibling / diamond 回收；
- 引入 node recycle 或 free list，评估回收成本；
- 处理 base neighbor、left/right neighbor 的互反更新；
- 支持 chunk boundary 的串行回退或边界约束；
- 对比 CPU DOD topology commit，说明 GPU 化是否值得继续推进。

验收标准：

- split / merge 都能在固定相机路径中稳定运行；
- active triangle、split、merge、invalid topology 与 DOD 对齐或差异可解释；
- 每个失败案例都有可复现 benchmark profile 和 debug readback 记录；
- 若成本高于 CPU DOD，应在报告中明确说明拓扑依赖是瓶颈。

### 验收标准

- 最低可交付：完成 4A 到 4D，GPU 至少承担 error evaluation 和 candidate marking，CPU topology commit 仍可保留；
- 推荐可交付：完成 4A 到 4F，GPU 路径能输出 GPU buffer 并减少 CPU mesh build / upload；
- 强展示项：完成 4G，使用 indirect draw 进一步降低 CPU 提交成本；
- 冲刺项：完成 4H 或 4I，尝试 GPU topology update 并记录局限；
- 三版本 benchmark 必须使用同一 Height Map、相机路径、核心参数和 CSV 字段；
- GPU 不可用、compute 不可用、indirect draw 不可用都必须被清晰标注为 skip / fallback，不允许静默退回 CPU 后当作 GPU 成绩；
- 所有阶段都要记录 CPU-GPU upload/readback bytes，避免 readback 成本掩盖 compute 收益；
- 任何 GPU 结果和 DOD/Classic 输出不一致时，都按 bug 处理，修复后写入 bug 修复记录。

## 阶段 5：实验、可视化与报告材料整理

### 目标

将“能跑”变成“能证明”。

### 任务

- 实现核心 Debug View；
- 固定相机路径回放；
- CSV 统计导出；
- 绘制性能曲线；
- 整理截图、视频和报告图表；
- 记录失败案例与降级原因。

### 验收标准

- 至少输出 2 到 3 张核心性能图；
- 至少输出 4 到 6 张 Debug / 最终画面截图；
- 有完整 CSV 日志；
- 结论基于数据，不只凭主观观感。
