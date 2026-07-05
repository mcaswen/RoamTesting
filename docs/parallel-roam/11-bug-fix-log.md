# Bug Fix Log

本文档记录 Parallel ROAM 开发过程中已经完成确认的 bug、易误判现象、定位过程、解决方案和验证方式。正在调查、尚未修复或用户未确认修复完成的问题，不写入正式记录。

## 记录规范

每条记录应包含：

- `状态`：`Open`、`Mitigated`、`Fixed`、`Won't Fix`、`Non-bug`
- `严重级别`：`高`、`中`、`低`
- `发生阶段`：bug 出现时所在的里程碑阶段、子阶段或功能分支
- `现象`：用户能观察到的表现
- `定位`：原因、相关模块和关键代码路径
- `Debug 过程`：关键假设、验证命令、探针结果、被排除的错误方向
- `解决方案`：已经实施的修复，或计划采用的修复
- `验证`：验证命令、数据、截图或人工检查方式
- `后续`：仍需补的测试、profiling 或架构调整

正式记录规则：

- 默认等用户明确确认“修复完了”之后再记录
- 不把调查中、尚未验证或刚发现的问题提前写成正式 bug log
- 每轮构建、smoke、benchmark 或回归测试后，如果结果与本轮预期不符且随后已修复，应作为 bug 记录
- 注释覆盖率、阶段标签、中文标点、连续注释块等规范门禁修复不算 bug，不写入本日志
- 如果中途出现错误判断，最终记录中要说明该方向为什么被排除
- 定位和解决方案不能只写结论，应记录关键代码路径、触发条件、为什么旧逻辑失败、为什么新逻辑能覆盖该场景
- 每条记录都要写清楚发生阶段，尤其是阶段性实现过程中引入的回归，便于回看里程碑风险
- 性能 bug 必须写清楚 Debug / RelWithDebInfo / Release 构建类型、关键参数和场景规模

## 当前问题清单

### BUG-001：RelWithDebInfo preset 构建成 bootstrap

- 状态：`Fixed`
- 严重级别：`中`
- 发生阶段：阶段 0 工程初始化与依赖接入，CMake preset 和 FetchContent 路径配置期间
- 现象：执行 `cmake --preset relwithdebinfo` 和 `cmake --build --preset relwithdebinfo` 后，程序只输出 `Parallel ROAM bootstrap`，GLM、stb、Dear ImGui 未链接，完整 app 没有启用
- 定位：默认 `relwithdebinfo` preset 没有开启 `PARALLEL_ROAM_FETCH_MISSING_DEPS`，本机又没有完整安装 GLM、stb、Dear ImGui，因此 CMake 进入 bootstrap fallback
- 解决方案：新增 `relwithdebinfo-fetch` 和 `release-fetch` preset，并添加 `scripts/run_relwithdebinfo_fetch.sh`、`scripts/run_release_fetch.sh`、`scripts/run_debug_fetch.sh`、`scripts/run_smoke_test_fetch.sh` 作为快捷入口
- 验证：`cmake --list-presets` 能列出 `relwithdebinfo-fetch` 和 `release-fetch`；`./scripts/run_smoke_test_fetch.sh` 能构建完整 app 并通过 smoke test
- 后续：若后续引入 vcpkg 为主路径，应补充 `relwithdebinfo-vcpkg` 脚本

### BUG-002：`--smoke-test` 下窗口闪退

- 状态：`Non-bug`
- 严重级别：`低`
- 发生阶段：阶段 0 工程初始化与最小渲染闭环，smoke test 自动化验证入口接入期间
- 现象：执行 `./build/debug-fetch/bin/ParallelROAM --smoke-test` 时，窗口出现一下就马上退出
- 定位：`--smoke-test` 是自动验证入口，目标是创建窗口、加载 OpenGL、渲染数帧后自动退出，用于 CI 或快速环境检查
- 解决方案：运行交互版本时不要带 `--smoke-test`；快捷脚本中 `run_smoke_test_fetch` 专门保留自动退出行为
- 验证：不带 `--smoke-test` 运行时窗口会持续打开；带 `--smoke-test` 时命令行输出 OpenGL renderer 和 version 后正常退出
- 后续：可以在启动日志中明确打印 `smoke test will exit automatically`，减少误判

### BUG-003：VSCode / clangd 报项目头文件找不到

- 状态：`Fixed`
- 严重级别：`中`
- 发生阶段：阶段 0 工程初始化，VSCode CMake Tools、clangd 和 `compile_commands.json` 配置期间
- 现象：编辑器报 `'app/CameraController.h' file not found clang(pp_file_not_found)`，但命令行构建可以通过
- 定位：clangd 没有读取到当前 build preset 生成的 `compile_commands.json`，因此不知道 `src/` include path 和 FetchContent 依赖 include path
- 解决方案：保证使用带完整依赖的 CMake preset 配置工程，并让 VSCode CMake Tools / clangd 使用对应 build 目录的 `compile_commands.json`
- 验证：重新配置 `debug-fetch` 或 `relwithdebinfo-fetch` 后，编辑器红线消失，命令行构建仍通过
- 后续：如红线再次出现，应检查 VSCode 当前选中的 CMake preset 和 clangd compile commands 路径

### BUG-004：开启 Classic ROAM 裂缝修复后仍产生 T-junction

- 状态：`Fixed`
- 严重级别：`高`
- 发生阶段：阶段 2 Classic CPU ROAM，2D 邻接关系与裂缝处理早期实现期间
- 现象：开启裂缝修复后探针仍能发现 T-junction，`center-default`、`near-corner`、`far` 场景都有残留裂缝候选
- 定位：早期 split 只处理当前节点的 `BaseNeighbor`，最终 leaf 邻接重建也只匹配完整共享边，无法发现“一条粗边被多条细边贴住”的情况
- 解决方案：Classic 节点改为裸指针拓扑，接入基于 `baseNeighbor` 的 diamond forced split 传播，并加入 T-junction repair pass 扫描粗细边贴合情况后继续 split
- 验证：使用临时探针检查多个相机位置的 T-junction 候选；UI 中可观察 forced split、constraint propagation 和 crack risk 统计
- 后续：当前 repair pass 偏全局扫描，后续应替换为边哈希或局部 diamond repair，避免性能问题

### BUG-005：Classic ROAM 的 PathId 撞号

- 状态：`Fixed`
- 严重级别：`高`
- 发生阶段：阶段 2 Classic CPU ROAM，2E merge / hysteresis 历史状态统计接入期间
- 现象：rootA 从 `1` 开始，rootB 从 `2` 开始，而 child 使用 `parentPathId * 2`，导致 rootA 的左 child 与 rootB 根节点同为 `2`
- 定位：两棵 root tree 的 path namespace 没有隔离，hysteresis 和 merge 统计会因为 ID 碰撞被污染
- 解决方案：将两棵 root tree 的 `PathId` 分区，rootA 使用低位起点，rootB 使用高位起点，child 仍保持 binary heap 风格派生
- 验证：检查 root 和 child path 生成逻辑，确认两棵 root tree 的子树不会落入同一 ID 区间
- 后续：后续 Data-Oriented 版本如果改用 index pool，应保留稳定路径 ID 或显式历史表

### BUG-006：ROAM 输出三角绕序反向

- 状态：`Fixed`
- 严重级别：`中`
- 发生阶段：阶段 2 Classic CPU ROAM，2C leaf triangle 输出到 render mesh 期间
- 现象：探针显示 ROAM 输出三角在 XZ 投影中与规则网格绕序相反；当前没开启 face culling 时不明显，但后续法线 debug 或背面剔除会出错
- 定位：ROAM domain triangle 的三维 emit 顺序没有统一到正 Y 方向
- 解决方案：在输出 render triangle 时检查三角法线方向，若法线不是正 Y 则交换顶点顺序
- 验证：探针检查 ROAM 三角绕序；规则网格 baseline 与 Classic ROAM 的面方向一致
- 后续：加入 face culling debug 开关时应再次验证

### BUG-007：Classic ROAM 近距离细分变化很小

- 状态：`Fixed`
- 严重级别：`高`
- 发生阶段：阶段 2 Classic CPU ROAM，2B split / 2C geometric error 和 screen error 估算期间
- 现象：打开 Classic ROAM 后，相机靠近地形时三角形数量变化很小，近处仍然没有明显细分
- 定位：split score 主要依赖 height error，平坦区域或 base 中点误差较低时，即使相机很近也缺少屏幕空间细分压力
- 解决方案：几何误差改为采样三条边中点和重心，并在 screen error score 中加入近距投影边长权重，同时提高默认 `MaxDepth`
- 验证：右侧 UI 中 active triangle、actual depth 和 split count 会随相机靠近更明显变化；wireframe 可观察近处网格密度提升
- 后续：后续应引入真正的屏幕空间误差投影，而不是当前经验权重

### BUG-008：FPS 最低只显示 10

- 状态：`Fixed`
- 严重级别：`中`
- 发生阶段：阶段 1 UI 面板和运行时统计接入，真实帧时间显示拆分期间
- 现象：UI 中 FPS 低到一定程度后始终显示 10，无法判断真实卡顿程度
- 定位：主循环原先将 delta time clamp 到 `0.1s` 后同时用于相机更新和 FPS 计算，导致显示 FPS 下限被固定为 `1 / 0.1 = 10`
- 解决方案：拆分 raw delta 和 clamped delta；相机移动使用 clamped delta，FPS 和 frame ms 使用 raw delta
- 验证：UI 显示真实 frame ms；当单帧超过 100ms 时 FPS 不再被钳制到 10
- 后续：需要补 profiling 面板，将算法重建、GPU 上传、绘制分开统计

### BUG-009：Classic ROAM 单帧耗时达到 4500ms

- 状态：`Mitigated`
- 严重级别：`高`
- 发生阶段：阶段 2 Classic CPU ROAM，早期全局 crack repair 路径
- 现象：开启 Classic ROAM 后一帧耗时可达到 4500ms 左右，交互几乎不可用
- 定位：Debug 构建下 `RepairCracksWithDiamondSplits` 和 `FindCrackRepairCandidates` 进行全局 leaf 对 leaf 扫描，主要复杂度接近 `O(P * L^2)`；`RebuildLeafNeighborLinks` 也有 `O(L^2)` 成本
- 解决方案：已新增 `relwithdebinfo-fetch` / `release-fetch` 快捷脚本，性能观察默认使用优化构建；渲染器已加入相机位移阈值缓存，避免静止或微小移动时每帧重建 Classic ROAM mesh
- 验证：临时 benchmark 显示 Debug 下高深度 crack repair 是主要瓶颈；优化构建耗时显著下降但全局扫描结构仍然存在
- 后续：需要用边哈希、空间哈希或局部 diamond repair 替换全局 T-junction 扫描；同时在 UI 中加入 build ms、repair pass、candidate count 等 profiling 指标

### BUG-010：持久化拓扑后相机移动不再继续细分

- 状态：`Fixed`
- 严重级别：`高`
- 发生阶段：阶段 2 Classic CPU ROAM，2J-2L 持久化拓扑、严格 diamond merge、拓扑验证入口接入之后
- 现象：完成 2J-2L 修改后，开启 Classic ROAM 时地形 LOD 表现几乎不再随相机变化；用户靠近地形、移动到不同位置后，wireframe 和地形三角形密度看起来没有继续更新；UI 中 `Split` 长时间为 `0`，容易误判为只是统计口径变化，但实际视觉上也没有新的细分发生
- 定位：根因在 `src/algorithms/classic_roam/ClassicRoamMeshBuilder.cpp` 的 `RefineWithSplitQueue`。2J-2L 把 Classic ROAM 从“每次 build 清空并从 root 重新细分”改成“持久化拓扑，普通相机移动复用已有 bintree”，但 split priority queue 的入口仍然只调用 `enqueueCandidate(rootA)` 和 `enqueueCandidate(rootB)`。`enqueueCandidate` 只接受 active leaf；第一轮构建后 root 已经 `IsSplit == true`，不再是 leaf，于是第二轮之后 root 会被直接拒绝，队列为空，当前 active leaf 根本没有机会按新相机位置重新计算 screen error。换句话说，持久化拓扑改变了 split 的入口集合，但候选队列仍按临时拓扑时代的 root-only 入口设计
- Debug 过程：最初根据 UI 中 `SplitCount == 0` 判断，怀疑是统计语义问题，因为持久拓扑后 `SplitCount` 表示“本次 build 新发生的 split”，不等于“当前拓扑里仍然展开的 split”。因此先补充 `ActiveSplitCount`，把 UI 拆成“活跃 Split”和“本帧 Split”。用户随后确认不是单纯 UI 问题，而是地形视觉真的不继续细分。继续检查渲染链路后排除了线框绘制模式、OpenGL draw call、mesh upload 和高度图资源路径：规则网格模式正常，Classic ROAM 初始 mesh 能生成，问题只发生在已有拓扑随相机继续更新的阶段。随后回到算法入口检查 `RefineWithSplitQueue`，发现队列只从 root 入队，而 root 在持久拓扑中通常已经是 internal node。该结论也解释了为什么 2J 之前不明显：旧实现每帧重建整棵树，root 每次都是 leaf，可以进入队列；持久化后 root 只在第一轮是 leaf，后续 build 的候选集合必须改为当前 active leaf 集合
- 解决方案：在 `RefineWithSplitQueue` 内新增 `enqueueActiveLeaves` 递归入口，从 `_rootA`、`_rootB` 向下遍历当前 active topology。遇到 active leaf 时再调用原有 `enqueueCandidate`，internal node 继续递归到 left / right child。这样保留 priority queue 的预算、score 排序和 forced split 后 child 重新入队逻辑，同时修正候选入口，使每一次相机触发 rebuild 时所有 active leaf 都能重新计算 `ComputeScreenErrorScore` 并决定是否继续 split。为避免后续再次误判，还新增 `ClassicRoamStats::ActiveSplitCount`，将状态统计和本帧事件统计分开；UI 显示“活跃 Split”和“本帧 Split”。另外新增正式的无窗口诊断入口 `src/benchmark/RoamProbe.cpp`，`main.cpp` 只做 `--roam-probe` 参数分发，避免把探针逻辑堆在入口文件里
- 验证：用户确认视觉问题已修复。命令行探针 `./build/debug-fetch/bin/ParallelROAM --roam-probe` 使用同一个 `ClassicRoamMeshBuilder` 连续跑多个相机位置，验证持久拓扑会继续变化：`far triangles=823 activeSplits=821 frameSplits=821`，`center triangles=6985 activeSplits=6983 frameSplits=6162`，`near-corner triangles=8188 activeSplits=8186 frameSplits=2237 frameMerges=1034`，`center-return triangles=9408 activeSplits=9406 frameSplits=1618 frameMerges=398`。这些数据证明不同相机位置下 active triangle、active split、本帧 split 和 merge 都会变化，不再停留在第一次 build 的拓扑。`./scripts/run_smoke_test_fetch.sh` 通过，`cmake --build --preset debug-fetch` 通过，注释覆盖率检查为 `15.04%`，注释末尾中文句号/逗号检查无命中
- 后续：需要把 `--roam-probe` 继续扩展成可复现 benchmark scenario，最好支持指定高度图、相机路径、阈值、最大深度和输出 CSV；后续每次修改 Classic ROAM 的 split / merge / hysteresis 逻辑时，应先跑该探针确认 active leaf 入口没有回归。还需要在阶段 2 完成固定测试入口，把“root-only 候选队列”这类拓扑入口错误变成自动化回归测试，而不是靠人工观察 wireframe

### BUG-011：DOD 候选标记 pass 拆文件后未进入 CMake target

- 状态：`Fixed`
- 严重级别：`中`
- 发生阶段：阶段 3，DOD ROAM 并行候选标记和内部架构拆分期间
- 现象：将 split / merge candidate marking 从 topology pass 中拆到新的 `DataOrientedRoamCandidateMarking.cpp` 后，`debug-fetch` 构建暴露出新源文件没有纳入 `parallel_roam` target 的问题，导致新 pass 的实现不能被链接进应用
- 定位：实现层已经在 `DataOrientedRoamState.h` 暴露 `CollectSplitCandidates`、`CollectMergeCandidates` 和 `CanMergeNode`，`DataOrientedRoamTopology.cpp` 也改为调用这些函数，但 `CMakeLists.txt` 的 app source 列表缺少 `src/algorithms/data_oriented_roam/DataOrientedRoamCandidateMarking.cpp`。头文件声明和调用点都存在时，构建系统遗漏源文件会让链接阶段找不到实现，属于拆模块时的构建集成错误
- Debug 过程：先确认新 pass 文件存在且函数签名与 header 一致，再检查 app target 的源文件列表。定位到 CMake source list 仍只包含原有 DOD 文件，没有包含新拆出的 candidate marking 文件。该问题不是算法逻辑错误，也不是 namespace 不一致，而是 build target 没有消费新增 cpp
- 解决方案：在 `CMakeLists.txt` 的 `parallel_roam` source 列表中加入 `src/algorithms/data_oriented_roam/DataOrientedRoamCandidateMarking.cpp`，保持拆分后的 pass 文件和 app target 同步
- 验证：`cmake --build --preset debug-fetch` 通过；随后 `./build/debug-fetch/bin/ParallelROAM --benchmark --algorithm all --profile smoke --csv /private/tmp/roam_3d_smoke_after_split.csv` 通过，Classic 与 DOD 的 smoke 场景三角形数保持一致，GPU 不可用时按 benchmark 逻辑跳过；`./scripts/run_smoke_test_fetch.sh` 通过
- 后续：每次新增 `.cpp` 文件时，构建验证应优先跑 `cmake --build --preset debug-fetch`，并在拆模块提交前检查 `CMakeLists.txt` 的 source list 是否同步

### BUG-012：DOD 保守并发拓扑提交在 smoke benchmark 中出现额外性能回退

- 状态：`Fixed`
- 严重级别：`中`
- 发生阶段：阶段 3，DOD ROAM 保守并发 topology commit 接入期间
- 现象：保守并发提交初版通过了 DOD smoke benchmark，三角形数和拓扑验证结果正确，但单独运行 `--benchmark --algorithm dod --profile smoke` 后，`near-corner` 和 `center-return` 的 Debug 构建耗时明显高于预期。功能正确但性能与“并发提交应减少 topology 压力”的预期不一致
- 定位：`BuildInteriorMergeChunks` 在分桶阶段对每个 merge candidate 调用完整 `SafeInteriorMergeChunkId`，而该函数内部又调用 `CanMergeNode`。`CanMergeNode` 会计算当前节点和 base neighbor 的 screen error；候选标记阶段已经计算过一次 merge score，分桶阶段再次对大量候选重复做评分，导致 Debug smoke 中 topology 时间被重复校验成本放大。问题不是线程数量不足，也不是 benchmark 三角形数错乱，而是 merge 分桶把“结构安全检查”和“提交前完整校验”混在了一起
- Debug 过程：先排除并发运行多个验证命令导致的 CPU 争用，因为单独跑 DOD smoke 仍能看到耗时偏高。随后对比优化前后 CSV 中 `cpuTopologyMs`、`cpuCollectMs` 和 `cpuUtilizationPercent`，确认额外成本集中在 topology 路径而不是 collect 或 error evaluation。检查 merge 分桶代码后发现候选筛选阶段重复执行 `CanMergeNode`，这与候选标记 pass 的职责重叠
- 解决方案：把 merge interior 判定拆成两层：`HasMergeReadyChildren` 和 `HasMergeReadyDiamond` 只做便宜的拓扑形状检查，用于候选分桶；worker 真正提交前再调用带完整 score 校验的 `SafeInteriorMergeChunkId(..., true)` 和原有 `MergeNodeOrDiamond`。这样分桶阶段不再重复计算大量 screen error，同时保留提交前的安全校验和串行回退
- 验证：优化后单独运行 `./build/debug-fetch/bin/ParallelROAM --benchmark --algorithm dod --profile smoke --csv /private/tmp/roam_3e_optimized_dod_smoke.csv` 通过，输出仍为 `far=823`、`center=6985`、`near-corner=8188`、`center-return=9408` 三角形，拓扑错误为 0。对应 CSV 中 DOD `cpuWorkerCount` 在中心和近景为 8，`cpuTopologyMs` 为 `far 7.18025`、`center 45.7321`、`near-corner 22.1229`、`center-return 12.3545`。`all smoke benchmark` 和 `./scripts/run_smoke_test_fetch.sh` 也通过
- 后续：当前保守并发策略仍会为 chunk 安全性付出额外判定成本，后续如果要继续优化，应把 chunk id 缓存到 node pool 或候选快照中，并把 interior / boundary 候选数量输出到 benchmark CSV，避免只能从 CPU 时间间接判断覆盖面

## 模板

```text
### BUG-XXX：标题

- 状态：
- 严重级别：
- 发生阶段：
- 现象：
- 定位：
- Debug 过程：
- 解决方案：
- 验证：
- 后续：
```
