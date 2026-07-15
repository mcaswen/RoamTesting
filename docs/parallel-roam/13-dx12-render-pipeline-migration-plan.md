# DX12 渲染与计算管线迁移总结

> 启动日期：2026-07-15  
> 完成日期：2026-07-16  
> 状态：阶段 0 至阶段 6 已完成，迁移阶段关闭  
> 后续工作：见 [CBT 2024 接入与复现计划](16-cbt-2024-integration-plan.md)

## 1. 完成结论

RoamTesting 已完成从 OpenGL 单一渲染路径向 DX12 主开发路径的迁移准备和功能接入。当前代码已经具备：

- SDL 窗口与图形后端生命周期分离；
- 可选择的 OpenGL/DX12 构建后端；
- DX12 设备、交换链、帧资源、描述符、围栏和调试层；
- 高度图地形、深度、纹理、线框、调试着色和 ImGui；
- Classic CPU ROAM 与数据导向 CPU ROAM 的 DX12 网格上传和绘制；
- GPU ROAM-like 的 DX12 Compute Shader、GPU 网格生成和 `ExecuteIndirect` 路径；
- DX12 GPU 时间戳、围栏等待统计、自动烟雾测试和运行时 benchmark；
- GPU 资源借用、生命周期和间接绘制数据包约定。

迁移阶段的目标已经达到：CBT 2024 不再需要先解决窗口、渲染、着色器、屏障、计时或基础间接绘制问题，可以作为独立算法接入任务推进。

## 2. 迁移后的实际架构

```text
Application
├── Window
│   └── SDL 窗口、事件和原生窗口句柄
├── IGraphicsBackend
│   ├── OpenGlGraphicsBackend
│   └── D3D12GraphicsBackend
│       ├── DXGI 适配器、设备和交换链
│       ├── 双帧命令分配器与围栏
│       ├── RTV、DSV 和可见描述符堆
│       ├── GPU 时间戳与延迟读回
│       └── ImGui DX12 命令记录
├── TerrainRenderer
│   ├── CPU 网格上传与直接绘制
│   ├── GPU 顶点/索引资源借用
│   └── DRAW_INDEXED 间接绘制
└── ITerrainLodAlgorithm
    ├── Classic CPU ROAM
    ├── 数据导向 CPU ROAM
    └── GPU ROAM-like（DX12 计算管线验证实现）
```

### 2.1 模块迁移结果

| 模块 | 迁移前 | 当前状态 |
|---|---|---|
| `platform/Window` | 同时拥有 SDL 窗口和 OpenGL 上下文 | SDL 窗口所有者，不再假设必然存在 GL 上下文 |
| `app/Application` | 直接清屏、交换缓冲和初始化 GLAD | 驱动统一帧流程，具体命令由图形后端处理 |
| `render/GraphicsBackend` | 不存在 | 定义应用所需的最小后端边界 |
| `render/D3D12GraphicsBackend` | 不存在 | 管理设备、交换链、帧资源、描述符、时间戳和围栏 |
| `gui/ImGuiLayer` | SDL2/OpenGL3 | 同时支持 SDL2/OpenGL3 和 SDL2/DX12 |
| `render/TerrainRenderer` | OpenGL VAO、缓冲和绘制 | 支持 DX12 CPU 网格和 GPU 间接绘制 |
| `ITerrainLodAlgorithm` | GPU 输出暴露 OpenGL 资源编号 | 增加原生 DX12 资源和生命周期约定 |
| GPU ROAM-like | OpenGL Compute Shader | 增加独立 DX12/HLSL 计算实现 |
| benchmark | OpenGL 查询和基础 CSV | 增加后端、适配器、驱动、围栏等待和渲染时间 |
| CMake | 默认依赖 OpenGL | 支持 `OpenGL` 和 `D3D12` 构建选择 |

## 3. 已完成的关键技术决策

### 3.1 保留最小后端抽象

项目没有建设完整的通用渲染硬件接口。`IGraphicsBackend` 只覆盖当前应用需要的：

- 窗口配置和图形初始化；
- 帧开始、界面记录、提交和呈现；
- 可绘制尺寸和 VSync；
- GPU 等待、时间戳和设备信息；
- GPU 算法所需的 DX12 原生命令上下文。

算法特有资源和拓扑状态仍由算法实例拥有，不下沉到通用后端。

### 3.2 CPU 算法不改拓扑语义

Classic CPU ROAM 和数据导向 CPU ROAM 保留原有拓扑实现，只替换网格上传、投影深度约定和绘制路径。这样迁移前后的节点数、三角形数、split/merge 和非法拓扑统计仍具有可比性。

### 3.3 GPU 资源由算法跨帧持有

[`TerrainLodRenderPacket`](../../src/algorithms/ITerrainLodAlgorithm.h) 只借用算法资源，不转移所有权。当前约定包括：

- 原生图形接口类型；
- 顶点、索引和间接命令资源；
- 资源容量；
- 有效生命周期；
- 资源代次；
- 活动三角形和索引数量。

renderer 在算法下一次构建或重置前使用这些资源，不能释放或按帧复制资源所有权。

### 3.4 普通帧不进行强制读回

DX12 时间戳和统计结果按帧延迟解析。普通渲染帧不等待 GPU 计数器或查询结果；仅初始化、显式验证和立即执行工具路径允许阻塞等待。

### 3.5 OpenGL 暂时保留为回归后端

OpenGL 不再是 CBT 2024 的目标实现后端，但仍保留用于：

- 对照迁移前行为；
- 回归 CPU 算法拓扑计数；
- 区分算法问题与 DX12 渲染问题。

是否删除 OpenGL、GLAD 和旧着色器，延后到 CBT 官方基线稳定之后决定。

## 4. 构建和着色器流程

构建变量：

```text
PARALLEL_ROAM_GRAPHICS_API=OpenGL|D3D12
```

DX12 路径使用 Windows SDK 中的 DXC 在构建阶段编译外部 HLSL。当前生成：

- 地形顶点和像素着色器；
- GPU ROAM-like 活动叶节点压缩；
- 误差评估和候选标记；
- split-only 拓扑更新；
- 网格生成和间接命令。

编译产物复制到应用运行目录，避免运行时依赖源码工作目录。

## 5. 分阶段完成记录

| 阶段 | 内容 | 状态 | 主要结果 |
|---:|---|---|---|
| 0 | 冻结 OpenGL 基线 | 已完成 | 固定截图、运行时 benchmark 和迁移前标签 |
| 1 | 拆分平台与后端边界 | 已完成 | Window、Application、ImGui 与图形后端解耦 |
| 2 | DX12 最小闭环 | 已完成 | 设备、交换链、命令列表、清屏、呈现和缩放 |
| 3 | 基础地形渲染 | 已完成 | 高度图、纹理、深度、线框和调试着色 |
| 4 | ImGui 与 CPU LOD | 已完成 | 两套 CPU ROAM 接入 DX12，支持切换和重置 |
| 5 | benchmark 与计时 | 已完成 | GPU 时间戳、围栏等待、自动输出和后端信息 |
| 6 | GPU 计算基础设施 | 已完成 | DX12 Compute Shader、GPU 网格和间接绘制闭环 |

### 5.1 阶段 0：冻结 OpenGL 基线

- 保存默认场景截图和固定相机 benchmark；
- 记录 Classic、DOD 和 GPU ROAM-like 的活动三角形与主要耗时；
- 创建 `pre-dx12-migration-opengl-baseline` 标签。

### 5.2 阶段 1：拆分平台和后端边界

- SDL 窗口生命周期与 OpenGL 上下文分离；
- `Application` 的清屏、呈现和后端初始化下沉；
- ImGui 初始化改为后端专用配置；
- CMake 增加构建后端选择。

### 5.3 阶段 2：DX12 最小闭环

- 创建 DXGI 工厂、适配器、设备、命令队列和交换链；
- 创建 RTV、深度资源、命令分配器、命令列表和围栏；
- 支持窗口缩放、清屏、呈现、VSync 和调试层；
- 增加 DX12 自动烟雾测试路径。

### 5.4 阶段 3：基础地形渲染

- 增加 HLSL、根签名和图形管线状态；
- 实现顶点、索引、常量和纹理上传；
- 修正 DX12 零到一深度、纹理方向和三角形绕序；
- 恢复线框、光照和调试颜色模式。

### 5.5 阶段 4：ImGui 与 CPU LOD

- 接入 ImGui SDL2/DX12 后端和字体描述符；
- 恢复运行参数、算法切换、统计和高度图切换；
- 接入 Classic CPU ROAM 与数据导向 CPU ROAM；
- 检查算法切换、重置和高度图重载时的资源生命周期。

### 5.6 阶段 5：benchmark 与计时

- 使用 DX12 时间戳记录渲染时间；
- 延迟解析 GPU 查询；
- CSV/Markdown 增加后端、适配器、驱动、围栏等待和渲染耗时；
- 支持无人值守运行、固定相机路径和非零失败退出码。

### 5.7 阶段 6：GPU 计算基础设施

- 实现 SRV/UAV、默认堆、上传、屏障和持久资源复用；
- 实现活动叶节点压缩、误差评估、候选标记和 split-only 更新；
- 实现 GPU 顶点/索引生成和 `DRAW_INDEXED` 间接命令；
- 将同步读回限制在延迟统计或验证路径。

## 6. 验证证据

迁移阶段已经通过以下检查：

- OpenGL RelWithDebInfo 构建和 GPU 烟雾测试；
- DX12 Debug/RelWithDebInfo 构建；
- DX12 窗口创建、固定帧自动退出和 ImGui 初始化；
- Classic、DOD 和 GPU ROAM-like 算法切换；
- DX12 GPU ROAM-like 计算和间接绘制；
- 自动运行时 benchmark 和 CSV/Markdown 输出；
- 固定窗口截图和 GPU 算法截图。

相关产物位于：

- `benchmark-output/migration-baseline-20260715/`；
- `benchmark-output/migration-dx12-20260716/`；
- `benchmark-output/dx12-stage4-algorithm-smoke.csv`；
- `benchmark-output/runtime-benchmark-20260716-*.csv/.md`。

## 7. 迁移阶段关闭后的技术边界

以下事项不再属于 DX12 迁移，而属于 CBT 2024 接入：

- Shader Model 6.6 和 64 位原子操作能力检查；
- OCBT 位域、压缩求和树和容量特化；
- 二分器槽位池、逻辑 `heapID` 和半边邻接；
- 完整 split、merge 和兼容关系传播；
- 程序化 `DRAW` 间接绘制；
- CBT 专用统计、验证和容量饱和测试；
- 官方场景复现和高度图地形适配。

这些任务统一转入 [CBT 2024 接入与复现计划](16-cbt-2024-integration-plan.md)。

## 8. 已知技术债务

### 8.1 GPU ROAM-like 不是 CBT 基线

当前 DX12 GPU ROAM-like 的主要角色是验证计算着色器、跨阶段屏障、GPU 网格生成、计时和间接绘制。它不具备 CBT 的 OCBT 空闲槽位选择、一般半边邻接和完整 split/merge 传播协议，不能作为 CBT 拓扑实现的替代品。

### 8.2 当前间接绘制契约只支持 `DRAW_INDEXED`

官方 CBT 使用程序化 `D3D12_DRAW_ARGUMENTS`，顶点着色器通过活动二分器索引读取物理槽位。接入阶段需要扩展 renderer 和渲染数据包，而不是强迫官方实现先生成传统索引网格。

### 8.3 设备能力检查需要扩展

基础 DX12 路径当前以 Feature Level 12_0 为最低要求。CBT 路径还需要单独检查 Shader Model 6.6、64 位 UAV 原子操作和相关资源能力。

### 8.4 OpenGL 清理暂缓

双后端会增加少量维护成本，但在 CBT 官方基线稳定前仍有回归价值。删除 OpenGL 不作为 CBT 接入前置条件。

## 9. 完成定义

DX12 迁移阶段满足以下完成条件：

- DX12 应用能够稳定创建、渲染、呈现和退出；
- 两套 CPU LOD 算法通过统一接口运行；
- GPU 计算、屏障、持久资源和间接绘制基础设施可用；
- benchmark 能区分算法、上传、渲染和同步等待；
- 普通帧没有未说明的强制 GPU 读回；
- OpenGL 基线和迁移产物可追溯；
- CBT 接入可以在不重做渲染管线的前提下开始。

## 10. 后续文档关系

- 研究问题与假设：[研究假设与验证计划](12-research-hypothesis-validation-plan.md)；
- 官方源码结构：[large_cbt 总体架构与关键算法路径参考](15-large-cbt-architecture-reference.md)；
- 下一阶段实施：[CBT 2024 接入与复现计划](16-cbt-2024-integration-plan.md)。
