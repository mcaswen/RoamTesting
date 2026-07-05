# Development Guidelines

本文档约定 Parallel ROAM 的目录、命名、注释、Git、个人开发流程和 AI 辅助开发规范。所有新增代码、资源、文档和实验数据都应遵守本规范。

## 1. 重点关注

### 本项目重点

- 目录规范：基本原则、当前目录结构、规范要求。
- 命名规范：通用命名和资源命名。
- Git 与版本管理规范。
- Benchmark scenario 和资源规范。
- Bug 记录规范：用户确认修复完成后再记录现象、定位、debug 过程、解决方案和验证方式。
- 架构边界：避免 GUI、算法、渲染和 profiling 混在一起。

### AI 辅助开发

- 可以使用 AI 辅助开发，但使用 AI 代码时必须 code review，检查是否符合当前项目规范、是否有明显逻辑谬误、是否不符合上下文。
- 使用 AI 前建议先把本开发规范上传至 AI 平台项目文件夹或当前对话框。

## 2. 目录规范

### 2.1 基本原则

1. 资源按类型和用途分类存放，不随意堆放在项目根目录。
2. 不出现 `New Folder`、`Test`、`Temp`、`Final`、`Latest`、`最终版`、`最新版` 等无意义文件夹名称。
3. 第三方资源、项目资源、源码、文档分开管理。
4. 一个资源只放一个确定位置，不在多个目录重复散落。
5. 临时验证文件在验证结束后删除或整理归档。

### 2.2 当前目录结构

```text
ParallelROAM/
├── CMakeLists.txt
├── CMakePresets.json
├── cmake/                  CMake helper modules
├── src/                    C++ source code
│   ├── app/                Application、main loop、input、camera
│   ├── platform/           SDL2、OpenGL context、filesystem
│   ├── render/             Shader、buffer、texture、renderer
│   ├── terrain/            HeightMap、TerrainConfig、sampling
│   ├── algorithms/         Terrain LOD algorithm interface and implementations
│   ├── gui/                ImGui layer and panels
│   ├── profiling/          CPU/GPU timers, stats, CSV export
│   ├── benchmark/          benchmark scenarios and camera paths
│   ├── resources/          runtime resource loading
│   └── util/               logger, assertions, math helpers
├── assets/                 project runtime assets
│   ├── heightmaps/
│   ├── textures/
│   ├── shaders/
│   ├── materials/
│   └── benchmark_paths/
├── docs/                   project documentation
├── scenarios/              benchmark scenario definitions
├── tests/                  unit / integration tests
├── external/               manually managed third-party dependencies
└── third_party/            vendored third-party source or assets
```

### 2.3 规范要求

1. C++ 业务源码统一放在 `src/`。
2. Shader 统一放在 `assets/shaders/`。
3. Height Map 统一放在 `assets/heightmaps/`。
4. Benchmark path 统一放在 `assets/benchmark_paths/` 或 `scenarios/`。
5. 项目文档统一放在 `docs/`。
6. 第三方依赖必须放在 `external/` 或 `third_party/`，并保留来源和许可证说明。
7. 新增文件时优先加入已有模块目录；确实没有合适目录时再新建，并使用有意义的模块名。
8. 不确定文件应该放在哪个文件夹时，先查本文档，仍不确定再让 AI 基于本文档给建议。

## 3. 命名规范

### 3.1 通用要求

1. 所有命名应见名知意，禁止模糊命名。
2. 不允许使用 `Test`、`New`、`AAA`、`Final`、`Manager2` 这类无信息量名称作为正式命名。
3. 文件名、类型名、资源名、scenario 名都应体现用途和模块归属。
4. 缩写只使用业内通用写法，如 `UI`、`ID`、`CPU`、`GPU`、`LOD`、`ROAM`、`SSBO`。

### 3.2 源码文件与类型命名

1. 主要类文件名与类名一致，例如 `HeightMap.h` / `HeightMap.cpp` 对应 `HeightMap`。
2. 类、结构体、枚举使用帕斯卡命名法，例如 `TerrainConfig`、`FrameStats`、`RenderPacket`。
3. 接口使用 `I` 前缀，例如 `ITerrainLodAlgorithm`。
4. 公共抽象类型应体现职责，不用一个类型名覆盖多个功能。
5. 文件扩展名统一使用 `.h` 和 `.cpp`；Shader 使用 `.vert`、`.frag`、`.comp`。

正确示例：

- `Application`
- `Window`
- `HeightMap`
- `TerrainRenderer`
- `AlgorithmRegistry`
- `ClassicRoamAlgorithm`
- `GpuTimerQuery`

错误示例：

- `Test`
- `GameManager2`
- `NewTerrain`
- `TempRenderer`
- `Utils2`

### 3.3 变量、函数和命名空间

1. 命名空间使用 `ParallelRoam::<Module>`，模块名与 `src/` 下目录对应。
2. 公共方法使用帕斯卡命名法，例如 `Initialize()`、`Update()`、`BuildRenderData()`。
3. 私有方法使用帕斯卡命名法，但应保持职责清晰，例如 `UpdateCamera()`、`CollectActiveLeaves()`。
4. 局部变量和参数使用驼峰命名法，例如 `deltaTime`、`cameraData`。
5. 私有成员字段使用 `_` + 驼峰命名法，例如 `_moveSpeed`、`_terrainConfig`。
6. 常量使用帕斯卡命名法，例如 `MaxWorkerCount`。
7. 宏和编译定义使用全大写蛇形命名，例如 `PARALLEL_ROAM_HAS_SDL2`。
8. 尽量避免 public 可变字段；跨类访问优先使用方法、只读属性或接口。

示例：

```cpp
namespace ParallelRoam::Terrain
{
class HeightMap
{
public:
    bool LoadFromFile(const std::filesystem::path& filePath);
    float SampleHeight(float u, float v) const;

private:
    std::vector<float> _heightValues;
    uint32_t _width = 0;
    uint32_t _height = 0;
};
}
```

### 3.4 资源命名

资源命名采用“前缀_模块_对象_用途”的方式，避免重名和混乱。

| 类型 | 前缀 | 示例 |
|---|---|---|
| Height Map | `Hm_` | `Hm_Terrain_Mountain_1025` |
| Texture | `Tex_` | `Tex_Terrain_Grass_Diffuse` |
| Material config | `Mat_` | `Mat_Terrain_DebugHeatmap` |
| Shader | `Shader_` | `Shader_Terrain_Lit` |
| Compute shader | `Comp_` | `Comp_Roam_ErrorEvaluation` |
| Benchmark scenario | `Scenario_` | `Scenario_Roam_Mountain_Flythrough` |
| Camera path | `Path_` | `Path_Benchmark_CanyonLoop` |
| CSV output | `Csv_` | `Csv_Benchmark_ClassicRoam_20260703` |
| Screenshot | `Shot_` | `Shot_Debug_LodHeatmap_Frame120` |

未列出的资源类型若无特殊情况，使用文件后缀名首字母大写作为前缀，例如 `Png_`、`Obj_`、`Json_`。

## 4. Git 与版本管理规范

1. 每次提交只做一类明确修改，避免把文档、资源、架构重构和算法修改混在一起。
2. 提交前检查 `git status`，确认没有误提交构建产物、临时文件、个人 IDE 配置。
3. 不随意重写已共享提交历史；本地临时提交整理前先确认不会丢失工作。
4. 修改公共模块前先记录影响范围。
5. 发生冲突时必须解决干净，不允许把冲突标记留在文件中。
6. 资源文件若体积较大，提交前先确认是否真的需要进入仓库。
7. 对第三方代码或资源必须保留许可证和来源链接。

推荐提交信息格式：

```text
feat/fix/update/chore: 中文信息
```

提交类型只使用下列四类：

- `feat`：新增功能、阶段能力或可运行路径
- `fix`：修复 bug、回归或错误行为
- `update`：调整已有功能、文档、参数、实验口径或架构细节
- `chore`：构建、脚本、资源整理、依赖和非功能性维护

提交信息要求：

1. 标题使用中文，格式固定为 `feat/fix/update/chore: 中文信息`，不再使用 scope。
2. 提交正文写两到三句中文，说明本次完成的阶段内容、影响范围和验证方式。
3. 每完成一个明确阶段或可验证子阶段后提交一次，避免多个阶段长期堆在同一个提交里。
4. 提交前统计未提交文件，确认没有构建产物、临时 CSV、个人 IDE 配置或无来源资源误入提交。

示例：

```text
feat: 接入 Classic ROAM 统一算法接口

完成阶段 2 的算法接口适配，Classic CPU ROAM 通过统一 Terrain LOD 边界输出渲染包和统计数据。
验证了 debug-fetch 构建、无窗口 benchmark 和 smoke test，后续 Data-Oriented 与 GPU 版本可复用同一接口。
```

## 5. 代码原则

### 5.1 基本原则

1. 一个类只负责一类明确功能，避免“上帝类”。
2. 超长文件要拆分，逻辑堆积超过可维护范围时必须重构。
3. 核心算法、渲染表现、数据配置、GUI 控制尽量分离。
4. 优先使用项目中已有框架和系统，不重复写功能类似的系统。
5. 组合优于继承。
6. 资源所有权优先使用 RAII，避免裸 `new/delete`。
7. 性能敏感路径要能被 profiler 观测，不凭感觉优化。

### 5.2 字段与访问控制

1. 默认使用 `private`。
2. 尽量避免 public 字段直接裸露给外部修改。
3. 跨类访问优先通过方法、只读 getter 或接口。
4. 指针 ownership 必须清晰：拥有关系使用 `std::unique_ptr` 或值类型；非拥有引用使用引用、指针或明确命名。
5. 全局状态必须谨慎使用；需要全局访问时优先通过 `ApplicationContext` 或显式依赖注入。

推荐写法：

```cpp
class CameraController
{
public:
    const CameraData& GetCameraData() const;
    void Update(const InputState& input, float deltaTime);

private:
    CameraData _cameraData;
    float _moveSpeed = 8.0f;
};
```

不推荐写法：

```cpp
class CameraController
{
public:
    CameraData cameraData;
    float moveSpeed;
};
```

### 5.3 方法规范

1. 方法名清晰表达行为。
2. 一个方法最好只做一件主要事情，不在一个函数里混输入、算法更新、渲染、GUI 刷新、CSV 写入。
3. 过长方法要拆分为多个方法。
4. 重复逻辑要抽取为一个方法或小工具。
5. 对性能敏感方法，避免隐式大拷贝和不必要的内存分配。

推荐示例：

```cpp
void Application::Tick(float deltaTime)
{
    PollEvents();
    UpdateCamera(deltaTime);
    UpdateAlgorithm(deltaTime);
    RenderFrame();
}
```

### 5.4 主循环使用规范

1. 主循环只保留必须逐帧执行的逻辑。
2. 不在每帧做高开销文件扫描、shader 编译、资源重复加载。
3. 需要缓存的引用提前缓存，不每帧现找。
4. 能用事件或 dirty flag 触发的逻辑，不硬塞进每帧更新。
5. Benchmark 模式下应减少无关 debug 输出，避免污染性能数据。

### 5.5 命名空间规范

命名空间应与代码文件所属目录对应：

```text
src/app/Application.h                  -> ParallelRoam::App
src/render/TerrainRenderer.h           -> ParallelRoam::Render
src/terrain/HeightMap.h                -> ParallelRoam::Terrain
src/algorithms/classic_roam/...        -> ParallelRoam::Algorithms::ClassicRoam
src/profiling/FrameProfiler.h          -> ParallelRoam::Profiling
```

### 5.6 编码规范

1. 所有文本文件保存为 UTF-8。
2. 源码标识符、文件名和构建脚本默认使用 ASCII；项目内业务注释优先使用中文，第三方 API 名称和固定英文术语可保留英文。
3. 头文件使用 `#pragma once`。
4. include 顺序建议为：当前头文件、项目头文件、第三方头文件、标准库头文件。
5. 不在头文件中引入不必要的重型依赖；能前向声明就前向声明。
6. 格式化风格以后通过 `.clang-format` 固化；在此之前保持现有文件局部风格一致。

### 5.7 注释规范

1. 删除 AI 代码残留的“新增”“1/2/3/4 序号”“引用某某头文件”等无意义注释。
2. 项目内业务注释使用中文，第三方库名、API 名、shader 术语等固定英文可保留英文。
3. 注释用于解释“为什么这样做”和“这里有什么约束”，不是重复代码表面意思。
4. 注释行结尾不使用中文句号，也不使用中英文逗号；短语和完整句子都优先不加行尾标点。
5. 公共边界类型、复杂类、结构体和枚举使用 Doxygen 风格注释。
6. `struct` 不能只给字段写注释，结构体本身也要说明用途和数据流位置。
7. 项目自有 C++ 源码注释覆盖率仍需大于等于 15%，但不允许用重复函数名、字段名或参数名的注释凑数。
8. 源码中连续 `//` 或 `///` 注释块原则上不超过 3 行；背景说明要拆到具体分支、循环、数据写入或异常处理附近。
9. 源码注释不写当前开发阶段、子阶段或里程碑编号，例如“阶段 2”“3B”“3C”；需要说明历史或计划时写在文档和提交信息里。
10. 公共方法只有在参数约束、调用顺序、生命周期、失败语义或跨模块边界不显然时才写方法注释。
11. 内部临时 helper 只在语义不明显时补注释，避免为简单聚合类型制造噪音。
12. 复杂方法中的关键步骤必须用 `//` 标明原理、用途或约束，尤其是 OpenGL 生命周期、线程同步、GPU/CPU 数据同步、ROAM 拓扑提交和跨平台分支。
13. 私有短方法一般不需要注释；如果逻辑复杂，应加简短说明或拆分。
14. 注释必须随代码更新，不能留下过期解释。

公共类型注释示例：

```cpp
/// @brief 地形 LOD 算法输出的紧凑渲染描述
///
/// RenderPacket 是算法层和渲染层之间的边界
/// 算法负责填充，渲染器负责消费，GUI 不应直接修改
struct RenderPacket
{
    RenderPacketMode Mode = RenderPacketMode::CpuMesh;
    uint32_t ActiveTriangleCount = 0;
};
```

公共方法注释示例，仅在返回值、失败语义或调用顺序不显然时使用：

```cpp
/// @brief 返回 false 时 errorMessage 必须给出可直接定位资源或 shader 阶段的原因
bool LoadFromFile(const std::filesystem::path& filePath, std::string* errorMessage);
```

私有方法注释示例：

```cpp
// 传播强制 split，直到相邻叶子满足无裂缝深度约束
void PropagateNeighborConstraints();
```

## 6. 架构与模块边界规范

### 6.1 分层建议

项目开发中，尽量区分以下层：

1. 平台层：SDL2、OpenGL context、filesystem、时间等平台细节。
2. 数据层：Height Map、TerrainConfig、BenchmarkScenario、CameraPath。
3. 算法层：Classic ROAM、Data-Oriented ROAM、GPU ROAM-like 等 LOD 决策。
4. 渲染层：shader、buffer、texture、terrain renderer、debug renderer。
5. 表现与控制层：GUI、debug view、benchmark 控制面板。
6. 观测层：CPU/GPU profiler、stats history、CSV export。

### 6.2 规范要求

1. GUI 不直接修改算法内部节点，通过 controller、配置对象或命令触发。
2. 数据配置与运行时状态分离，不把动态状态写回原始资源。
3. 模块之间优先通过接口、事件、controller 通信，避免互相硬引用。
4. 公共系统不依赖某个具体算法实现。
5. 主循环回调只负责触发入口，复杂业务逻辑放到对应类和方法中。
6. 算法层不直接创建窗口、不直接处理平台 event、不直接绘制 GUI。
7. 渲染层不决定 LOD，只消费算法输出的 `RenderPacket`。

## 7. Benchmark 数据与资源规范

### 7.1 Benchmark scenario 规范

1. Benchmark scenario 统一放在 `scenarios/`。
2. Scenario 文件名使用 `Scenario_模块_场景名`，例如 `Scenario_Roam_Mountain_Flythrough.json`。
3. Scenario 应记录 Height Map、相机路径、分辨率、算法参数、记录时长和 warm-up 时长。
4. 不直接覆盖已经用于正式 benchmark 记录的 scenario；需要实验变体时复制一份并改名。
5. 提交前清理临时 scenario 和无效路径文件。

### 7.2 资源规范

1. 源文件和正式使用文件尽量分开存放。
2. 重复资源、废弃资源、无效资源及时清理。
3. 替换旧资源时必须记录说明，不要直接覆盖但不说明。
4. 正式资源与临时占位资源应区分清楚，防止误用。
5. 资源命名和存放规范参考本文档第 2、3 节。

## 8. 资源导入规范

1. 导入资源时记录来源、许可证和用途。
2. 不清楚许可证的资源不要进入正式仓库。
3. 大体积资源提交前先确认是否必须纳入版本管理。
4. 贴图、Height Map、shader、benchmark path 都应放入对应目录。
5. 第三方资源不混入项目自制资源目录。

## 9. 个人开发流程

### 9.1 开始前

1. 明确本次任务属于文档、构建系统、框架、算法、渲染、GUI、profiling 还是 benchmark。
2. 修改前先看对应模块文档和已有代码风格。
3. 涉及公共接口时，先确认调用方和输出数据结构。
4. 涉及 benchmark 数据时，先确认配置和相机路径是否需要保持可复现。

### 9.2 完成前

1. 使用 AI 辅助开发时，应把本规范提供给 AI，并对生成代码进行 code review。
2. 配置项、资源路径、工具说明应尽量明确，方便未来维护。
3. 不把测试代码、临时输出、硬编码实验逻辑留在正式版本里。
4. 修改公共逻辑时应在提交说明或文档中记录影响范围。
5. 所有新增业务逻辑和资源操作都在 Git 仓库中统一进行，不在本地长期游离开发。

### 9.3 Bug 记录流程

1. Bug 仍在调查或用户未确认修复完成时，不写入 `docs/parallel-roam/11-bug-fix-log.md` 的正式记录。
2. 只有当用户明确表示 bug 已修复、可以记录，或任务目标本身就是整理已完成问题时，才追加 bug log。
3. 记录必须尽量详细，包含状态、严重级别、发生阶段、现象、定位、debug 过程、解决方案、验证方式和后续项。
4. 发生阶段要写清楚对应里程碑、子阶段或功能分支，例如“阶段 2，2J-2L 持久化拓扑接入后”。
5. 定位字段要体现完整排查路径，包括关键假设、被排除的原因、临时探针或 benchmark 结果，以及最终指向的代码路径。
6. 解决方案字段要写清楚修复逻辑，不能只写“修复了某函数”；应说明旧逻辑为什么失败、新逻辑如何覆盖触发场景、影响哪些模块。
7. 性能问题要记录构建类型、关键参数、场景规模和测量方式，避免 Debug 数据被误当成 Release 数据。
8. 临时探针、一次性 benchmark 或手工验证结果可以写入定位和验证字段；如果只是一次性验证，不要进入正式源码目录；如果需要长期保留，应放入 `benchmark/`、`profiling/` 或 `tests/` 等对应模块。

## 10. 主要注意风险点

1. 对 AI 生成的代码不检查就直接使用。
2. 直接覆盖已稳定模块或正式 benchmark scenario。
3. Git 冲突未解决干净，把冲突痕迹留在项目中。
4. 复制 GPL 等强传染性许可证代码到项目源码中。
5. 把临时 debug 输出、临时资源、临时 benchmark 数据提交进正式目录。
6. GUI、算法、渲染、profiling 边界混乱，导致后续多算法切换困难。
7. Benchmark 没有固定配置和相机路径，导致性能数据不可复现。
8. 未经用户确认修复完成就提前写 bug log，导致记录不准确。
