# Dependency Setup

> 主分支不再配置或构建 GPU ROAM-like compute 路径；该实验保留在 `archive/gpu-roam-like` 分支。当前 OpenGL terrain 路径使用 4.1 core，D3D12 GPU 资源仅由 CBT 程序化绘制使用。

本文档记录 Parallel ROAM 的第三方依赖策略。当前目标是：源码仓库签出后，在目标机已经具备 C++20 编译器、系统 SDK 和显卡驱动的前提下，不依赖联网下载第三方库即可配置和编译。

项目已经内置：

- 运行/编译依赖源码：`third_party/`
- Windows portable CMake：`tools/cmake/bin/cmake.exe`
- OpenGL loader 预生成源码：`third_party/glad/`

编译器、Windows SDK、macOS Command Line Tools、Linux OpenGL/Mesa 开发包和显卡驱动不适合随仓库分发，仍由目标机器提供。

## 当前依赖

| 依赖 | 用途 | 默认来源 | 备用来源 | 备注 |
|---|---|---|---|---|
| CMake | 配置和生成构建系统 | Windows 使用 `tools/cmake`，其他平台使用系统 CMake | Windows 可运行 `scripts/setup_portable_cmake.bat` 刷新 | 版本 pin 到 3.30.5 |
| SDL2 | 窗口、输入、OpenGL context | 系统包优先，缺失时使用 `third_party/SDL2` | vcpkg / FetchContent | 项目当前使用 SDL2，不切 SDL3 |
| OpenGL | 渲染 API | 系统 SDK / 驱动 | 无 | macOS 自带 OpenGL 版本可能不足 4.3 |
| GLM | 数学库 | 系统包优先，缺失时使用 `third_party/glm` | vcpkg / FetchContent | header-only |
| GLAD | OpenGL function loader | `third_party/glad` | vcpkg / FetchContent 生成 | 已提交 OpenGL core 4.3 loader |
| stb | Height Map / image loading | `third_party/stb` | system headers / FetchContent | header-only |
| Dear ImGui | GUI/debug panels | `third_party/imgui` | vcpkg / FetchContent | 按构建后端选择 SDL2 + OpenGL3 或 SDL2 + DX12 backend |
| D3D12 Agility SDK | 固定 CBT 运行时和 Shader Model 6.6 能力 | `third_party/microsoft/d3d12-agility-sdk/1.614.1` | Microsoft NuGet | 与 CBT 2024 官方实现保持一致 |
| DirectX Shader Compiler | 编译 DX12/HLSL 着色器 | `third_party/microsoft/dxc/1.7.2308.12` | Microsoft NuGet | `dxcompiler.dll` 版本为 1.7.2308.7 |

## CBT DX12 固定依赖

D3D12 构建固定使用 CBT 2024 官方实现相同的 Agility SDK 和 DXC。首次配置前运行：

```powershell
powershell -ExecutionPolicy Bypass -File scripts/setup_cbt_dx12_dependencies.ps1
```

脚本从 Microsoft 官方 NuGet 下载固定包，校验包和关键二进制的 SHA-256，并解压到 Git 忽略的本地依赖目录。重复执行时，已通过校验的文件不会重新下载。需要恢复损坏或被替换的文件时使用：

```powershell
powershell -ExecutionPolicy Bypass -File scripts/setup_cbt_dx12_dependencies.ps1 -Force
```

D3D12 CMake 配置会检查固定文件、版本和哈希。缺失或不匹配时直接停止配置，不会静默回退到 Windows SDK 中的其他 DXC。

应用导出与 CBT 官方实现相同的 `D3D12SDKVersion=614` 和 `D3D12SDKPath=.\\D3D12\\`。按照 Microsoft 的 Agility SDK 加载规则，如果操作系统内置的 `D3D12Core.dll` 更新，系统版本会优先于应用携带的 1.614.1 运行时。项目会在启动日志和 benchmark 图形版本字段中记录实际加载的 D3D12Core 路径与文件版本；同一机器上的官方程序和接入版应以该实际值作为运行时对照。

## 默认路径：项目内依赖

普通 preset 默认不会主动联网。CMake 会先尝试系统包，再使用项目内的 `third_party` 源码兜底：

```sh
cmake --preset debug
cmake --build --preset debug
```

Windows CMD 可直接运行：

```bat
scripts\run\opengl\run_debug_fetch.bat --smoke-test
```

虽然脚本名保留了 `fetch`，但在 `third_party` 依赖齐全时不会下载 SDL2、GLM、stb 或 Dear ImGui；只有本地和系统依赖都缺失时，`PARALLEL_ROAM_FETCH_MISSING_DEPS=ON` 才会触发 FetchContent 兜底。

## 可选路径：vcpkg

仓库提供 `vcpkg.json` manifest：

```json
{
  "dependencies": [
    "sdl2",
    "glm",
    "glad",
    "stb",
    {
      "name": "imgui",
      "features": ["opengl3-binding", "sdl2-binding"]
    }
  ]
}
```

配置方式：

```sh
cmake --preset debug-vcpkg
cmake --build --preset debug-vcpkg
```

需要提前设置 `VCPKG_ROOT`，并保证 `VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake` 存在。

## Windows 无系统 CMake 时

仓库已经内置 Windows x86_64 portable CMake：

```text
tools\cmake\bin\cmake.exe
```

`.bat` 构建脚本会优先使用这份项目内 CMake；如果它不存在，才会尝试系统 `PATH` 里的 `cmake`，两者都不可用时再自动调用下载脚本。

需要刷新 CMake 时，可以在项目根目录运行：

```bat
scripts\setup_portable_cmake.bat
```

该脚本会下载 CMake Windows x86_64 zip，并恢复同样的目录结构。

## 备用路径：FetchContent

FetchContent 不作为默认路径，必须显式启用：

```sh
cmake --preset debug-fetch
cmake --build --preset debug-fetch
```

D3D12 使用独立 preset。固定 Agility SDK 和 DXC 已准备完成后，可运行：

```powershell
.\tools\cmake\bin\cmake.exe --preset relwithdebinfo-d3d12-fetch
.\tools\cmake\bin\cmake.exe --build --preset relwithdebinfo-d3d12-fetch --parallel
```

## 快捷构建脚本

仓库将启动脚本按后端放在 `scripts/run/opengl` 与 `scripts/run/d3d12`，脚本会自动执行 configure、build 和 run。OpenGL 在 macOS / Linux 使用 `.sh`，Windows 可使用 PowerShell `.ps1` 或 CMD `.bat`；D3D12 仅提供 Windows 入口。

OpenGL：

```sh
./scripts/run/opengl/run_debug_fetch.sh
./scripts/run/opengl/run_relwithdebinfo_fetch.sh
./scripts/run/opengl/run_release_fetch.sh
./scripts/run/opengl/run_smoke_test_fetch.sh
```

```powershell
powershell -ExecutionPolicy Bypass -File scripts/run/opengl/run_debug_fetch.ps1
powershell -ExecutionPolicy Bypass -File scripts/run/opengl/run_relwithdebinfo_fetch.ps1
powershell -ExecutionPolicy Bypass -File scripts/run/opengl/run_release_fetch.ps1
powershell -ExecutionPolicy Bypass -File scripts/run/opengl/run_smoke_test_fetch.ps1
```

```bat
scripts\run\opengl\run_debug_fetch.bat
scripts\run\opengl\run_relwithdebinfo_fetch.bat
scripts\run\opengl\run_release_fetch.bat
scripts\run\opengl\run_smoke_test_fetch.bat
```

D3D12 首次运行前需要先准备固定依赖，之后可使用：

```powershell
powershell -ExecutionPolicy Bypass -File scripts/run/d3d12/run_debug_fetch.ps1
powershell -ExecutionPolicy Bypass -File scripts/run/d3d12/run_relwithdebinfo_fetch.ps1
powershell -ExecutionPolicy Bypass -File scripts/run/d3d12/run_release_fetch.ps1
powershell -ExecutionPolicy Bypass -File scripts/run/d3d12/run_smoke_test_fetch.ps1
```

```bat
scripts\run\d3d12\run_debug_fetch.bat
scripts\run\d3d12\run_relwithdebinfo_fetch.bat
scripts\run\d3d12\run_release_fetch.bat
scripts\run\d3d12\run_smoke_test_fetch.bat
```

脚本与 preset 对应关系：

| 脚本 | OpenGL preset | D3D12 preset | 用途 |
|---|---|---|---|
| `run_debug_fetch` | `debug-fetch` | `debug-d3d12-fetch` | 断点调试和崩溃定位，性能数据不可信 |
| `run_relwithdebinfo_fetch` | `relwithdebinfo-fetch` | `relwithdebinfo-d3d12-fetch` | 日常运行、性能观察和 profiler 分析 |
| `run_release_fetch` | `release-fetch` | `release-d3d12-fetch` | 接近发布配置的最高优化运行 |
| `run_smoke_test_fetch` | `debug-fetch` | `debug-d3d12-fetch` | 快速验证窗口、后端初始化和资源加载的最小闭环 |

`.sh`、`.ps1` 和 `.bat` 脚本都会把额外命令行参数透传给 `ParallelROAM`。例如：

```sh
./scripts/run/opengl/run_relwithdebinfo_fetch.sh --smoke-test
```

```bat
scripts\run\d3d12\run_relwithdebinfo_fetch.bat --smoke-test
```

D3D12 构建可以独立验证 CBT OCBT 的四种容量特化。该入口不会初始化 terrain renderer 和 GUI，会检查空树、满树、边界位、交替位图和随机更新：

```powershell
.\build\debug-d3d12-fetch\bin\ParallelROAM.exe --cbt-ocbt-smoke-test
```

基础二分器资源入口会创建完整拓扑、任务、索引和命令缓冲，验证四档容量的初值上传、选择性读回，并从 1M 切回 128K 检查资源重建：

```powershell
.\build\debug-d3d12-fetch\bin\ParallelROAM.exe --cbt-base-topology-smoke-test
```

下面的窗口入口会运行完整 CBT 动态拓扑与程序化间接绘制 smoke，覆盖 split/merge 往返、容量和高度图切换、三种诊断模式以及 ModifiedOnly/FullDebug 几何：

```powershell
.\build\debug-d3d12-fetch\bin\ParallelROAM.exe --cbt-procedural-smoke-test
```

运行包含 Classic、Data-Oriented 与 CBT 2024 的 D3D12 runtime benchmark：

```bat
scripts\run\d3d12\run_relwithdebinfo_fetch.bat --runtime-benchmark
```

该命令会让每种可用算法依次执行同一组离散相机采样点，生成 `benchmark-output/runtime-benchmark-*.md` 和对应逐点 CSV 后自动退出。默认选项路径为 600 点并预热 16 帧，极限压力路径为 64 点并预热 24 帧，也可通过 `--runtime-benchmark-samples` 覆盖；算法耗时只改变完成整轮测试所需的墙钟时间，不改变采样点数量或姿态。GPU capability 不满足时，报告会保留 CPU 结果并写明 CBT skip 原因；D3D12 能力满足时，CBT 会记录延迟诊断和 18 项 GPU 阶段计时。

只运行 CBT、保留相同 runtime benchmark 链路：

```bat
scripts\run\d3d12\run_relwithdebinfo_fetch.bat --runtime-benchmark --runtime-benchmark-algorithm cbt
```

阶段 I 的正式四容量基线使用专用 PowerShell 入口。它会先核对 benchmark 标签、干净 tracked worktree 和冻结输入哈希，再构建并执行默认/极限路径、四档容量和三次重复：

```powershell
git switch --detach benchmark/cbt-2024-official-baseline-v1
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/benchmark/d3d12/run_cbt_2024_official_baseline_v1.ps1
```

正式汇总位于 `benchmark-output/cbt-2024-official-baseline-v1/`，逐帧原始报告位于其 Git 忽略的 `raw/` 子目录。固定身份、结果解释和上游差异见 [`18-cbt-2024-official-baseline-v1.md`](18-cbt-2024-official-baseline-v1.md)。

当前 pin 的版本：

```text
CMake    3.30.5 windows-x86_64
SDL2      release-2.32.10
GLM       1.0.3
GLAD      v2.0.8
Dear ImGui v1.92.8
stb       31c1ad37456438565541f4919958214b6e762fb4
D3D12 Agility SDK 1.614.1
DXC       1.7.2308.12（dxcompiler.dll 1.7.2308.7）
```

GLAD 已经使用官方生成器生成 OpenGL core 4.3 loader，并放在 `third_party/glad/`。CMake 会优先使用这份本地源码，因此普通构建不需要 Python 或 `jinja2`。

## 当前验证状态

截至 2026-08-25，当前 Windows / NVIDIA GeForce RTX 5090 D 环境已验证：

```text
relwithdebinfo-fetch:
- OpenGL 4.1 application and Classic/DOD runtime path passed
- SDL2、GLAD、GLM、stb 和 Dear ImGui local dependency path passed
- OpenGL public regression: 18/18 CTest passed

relwithdebinfo-d3d12-fetch:
- D3D12 application, adapter, UI and runtime benchmark passed
- CBT OCBT 128K、256K、512K、1M CPU/GPU validation and dynamic smoke passed
- default/extreme runtime quick、FullDebug geometry and frozen baseline verification passed
- D3D12 regression: 26/26 CTest passed
- C++/HLSL comment coverage gate passed
```

`debug-vcpkg` 和 `debug-d3d12-vcpkg` 尚未在当前机器验证，因为当前环境没有设置 `VCPKG_ROOT`。macOS OpenGL 4.1 的历史验证仅用于 capability skip 回归，不再代表当前主要开发环境。

## 平台注意事项

- Windows：仓库已内置 portable CMake 和第三方源码，但仍需要 MSVC/clang 等 C++20 编译器、Windows SDK 和可用的 OpenGL 驱动。
- macOS：系统 OpenGL 4.1 可运行 Classic/DOD；CBT 2024 只在 Windows D3D12 构建中启用。
- Linux：仍需要系统提供 C++20 编译器、CMake、OpenGL/Mesa 开发包和图形驱动。
- GLAD：当前仓库已包含 OpenGL core 4.3 loader；若后续改 OpenGL 版本或 extension 集合，需要重新生成。
- SDL3：官方最新稳定版本已经是 SDL3，但本项目当前选择 SDL2，原因是参考项目、ImGui backend 和现有代码计划都以 SDL2 为主。
