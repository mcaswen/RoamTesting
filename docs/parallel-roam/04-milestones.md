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

完成可工作的 Classic CPU ROAM 最小闭环。

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

- 引入 `baseNeighbor`；
- 实现 forced split；
- 可视化 neighbor link；
- 处理常见 T-junction；
- 条件允许时再引入 `leftNeighbor/rightNeighbor` 和完整 diamond split。

2E：merge 与 hysteresis

- 引入 `splitThreshold > mergeThreshold`；
- 相机远离时 merge；
- 防止频繁 split/merge 抖动；
- 记录 splitCount / mergeCount。

### 验收标准

- 近处和地形变化大的区域明显细分；
- 远处保持粗网格；
- wireframe 可清晰展示 LOD；
- 基本无裂缝；
- 能输出 active triangle count；
- 可作为三版本视觉一致性的 baseline。

## 阶段 3：Data-Oriented CPU 版本

### 目标

在相同误差阈值、Height Map 和相机路径下，以数据导向方式重构 ROAM，验证 CPU 多核与数据布局收益。

### 子阶段

3A：指针树转 Index-Based Node Pool

- 将 child / neighbor 从指针改为 index；
- 使用预分配 node pool；
- 避免运行期频繁 new/delete；
- 保留与 Classic 版一致的 split 语义。

3B：AoS 转 SoA

- 将 error、depth、flags、neighbor indices 分离；
- 只在需要时读取对应数组；
- 对齐 / padding 进行基本检查；
- 保证结果与 Classic 版一致或可解释地接近。

3C：线程池与并行误差评估

- 实现轻量线程池或使用 EnTT/task scheduler；
- `ErrorEvaluationSystem` 并行；
- 记录单线程与多线程耗时；
- 每阶段记录 CPU 时间。

3D：并行标记与收集

- 并行标记 split / merge candidate；
- thread-local buffer 收集 active leaves；
- 合并结果；
- 避免共享 vector 锁竞争。

3E：拓扑提交策略

- 初版保留单线程 topology commit；
- 可选：按 Terrain Chunk 分区处理；
- 可选：边界采用 skirt 或边界约束，降低跨 chunk 同步复杂度。

### 验收标准

- 视觉结果与 Classic 版相同或接近；
- 在中等/大规模场景下更新耗时优于 Classic 版；
- 有可展示的阶段时间分解；
- 能说明哪些环节并行收益高、哪些环节被拓扑依赖限制。

## 阶段 4：GPU ROAM-like 版本

### 目标

建立 GPU Compute 版本，优先迁移高并行、低依赖阶段。

### 子阶段

4A：GPU Error Evaluation

- 将 Height Map 作为 texture；
- 将 node 数据上传 SSBO；
- Compute Shader 计算 screen-space error；
- CPU 与 GPU error 值抽样比对；
- 使用 OpenGL Timer Query 记录 compute 时间。

4B：GPU LOD Candidate Marking

- GPU 根据 threshold 写 splitFlag / mergeFlag；
- CPU 读取少量结果或统一提交；
- 对比 CPU candidate marking 耗时。

4C：GPU Active Leaf Compaction

- Compute Shader 遍历节点；
- 收集 active leaf index；
- 使用 SSBO / atomic counter；
- CPU 仅读取计数和必要 metadata。

4D：GPU-Driven Rendering

- 用 active leaf index 生成绘制数据；
- 尝试 `DrawArraysIndirect` 或 `DrawElementsIndirect`；
- 降低 CPU 每帧构建和提交渲染数据的成本。

4E：GPU Topology Update（可选冲刺）

- GPU 端 split-only；
- 后续再尝试 merge；
- 使用多轮 Compute Pass 传播 forced split；
- 研究邻接深度差约束；
- 记录局限和失败案例。

### 验收标准

- GPU 至少完成 error evaluation 和 candidate marking；
- 能对比 Classic / Data-Oriented / GPU 的 CPU update 成本；
- 若完成 indirect draw，则作为重要加分；
- 若完成 GPU split-only 或完整拓扑更新，则作为冲刺亮点。

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
