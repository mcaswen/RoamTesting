# Dependency Setup

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
| Dear ImGui | GUI/debug panels | `third_party/imgui` | vcpkg / FetchContent | 本地构建 SDL2 + OpenGL3 backend |

## 默认路径：项目内依赖

普通 preset 默认不会主动联网。CMake 会先尝试系统包，再使用项目内的 `third_party` 源码兜底：

```sh
cmake --preset debug
cmake --build --preset debug
```

Windows CMD 可直接运行：

```bat
scripts\run_debug_fetch.bat --smoke-test
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

## 快捷构建脚本

仓库提供 `scripts/` 下的快捷脚本，脚本会自动执行 configure、build 和 run。macOS / Linux 使用 `.sh`，Windows 可使用 PowerShell `.ps1` 或 CMD `.bat`。

```sh
./scripts/run_debug_fetch.sh
./scripts/run_relwithdebinfo_fetch.sh
./scripts/run_release_fetch.sh
./scripts/run_smoke_test_fetch.sh
```

```powershell
powershell -ExecutionPolicy Bypass -File scripts/run_debug_fetch.ps1
powershell -ExecutionPolicy Bypass -File scripts/run_relwithdebinfo_fetch.ps1
powershell -ExecutionPolicy Bypass -File scripts/run_release_fetch.ps1
powershell -ExecutionPolicy Bypass -File scripts/run_smoke_test_fetch.ps1
```

```bat
scripts\run_debug_fetch.bat
scripts\run_relwithdebinfo_fetch.bat
scripts\run_release_fetch.bat
scripts\run_smoke_test_fetch.bat
```

脚本选择建议：

| 脚本 | CMake preset | 用途 |
|---|---|---|
| `run_debug_fetch` | `debug-fetch` | 断点调试和崩溃定位，性能数据不可信 |
| `run_relwithdebinfo_fetch` | `relwithdebinfo-fetch` | 日常运行、性能观察和 profiler 分析 |
| `run_release_fetch` | `release-fetch` | 接近发布配置的最高优化运行 |
| `run_smoke_test_fetch` | `debug-fetch` | 快速验证窗口、OpenGL、资源加载的最小闭环 |

`.sh`、`.ps1` 和 `.bat` 脚本都会把额外命令行参数透传给 `ParallelROAM`。例如：

```sh
./scripts/run_relwithdebinfo_fetch.sh --smoke-test
```

```bat
scripts\run_relwithdebinfo_fetch.bat --smoke-test
```

支持 OpenGL 4.3 Compute Shader 的机器还可以运行 GPU 专用 smoke test。该入口会连续强制重建 32 帧，覆盖 GPU split-only topology、active leaf compaction、mesh emit 和 indirect draw：

```bat
scripts\run_relwithdebinfo_fetch.bat --gpu-smoke-test
```

运行完整的 Classic、Data-Oriented、GPU 三算法 runtime benchmark：

```bat
scripts\run_relwithdebinfo_fetch.bat --runtime-benchmark
```

该命令会为每种可用算法运行同一条 10 秒相机路径，生成 `benchmark-output/runtime-benchmark-*.md` 和对应逐帧 CSV 后自动退出。GPU capability 不满足时，报告会保留 CPU 结果并写明 GPU skip 原因。

当前 pin 的版本：

```text
CMake    3.30.5 windows-x86_64
SDL2      release-2.32.10
GLM       1.0.3
GLAD      v2.0.8
Dear ImGui v1.92.8
stb       31c1ad37456438565541f4919958214b6e762fb4
```

GLAD 已经使用官方生成器生成 OpenGL core 4.3 loader，并放在 `third_party/glad/`。CMake 会优先使用这份本地源码，因此普通构建不需要 Python 或 `jinja2`。

## 当前验证状态

在当前 macOS 环境中：

```text
default debug:
- OpenGL linked
- SDL2 linked through CMake config on this machine
- GLAD linked from `third_party/glad`
- GLM linked from `third_party/glm`
- stb linked from `third_party/stb`
- Dear ImGui linked from `third_party/imgui`

debug-fetch:
- OpenGL linked
- SDL2 linked through CMake config on this machine
- GLM linked
- GLAD linked from `third_party/glad`
- stb linked
- Dear ImGui linked
```

`debug-vcpkg` 尚未在当前机器验证，因为当前环境没有设置 `VCPKG_ROOT`。

## 平台注意事项

- Windows：仓库已内置 portable CMake 和第三方源码，但仍需要 MSVC/clang 等 C++20 编译器、Windows SDK 和可用的 OpenGL 驱动。
- macOS：系统 OpenGL 可能无法满足 OpenGL 4.3 Compute Shader；GPU ROAM-like 阶段需要运行时检查 OpenGL version。
- Linux：仍需要系统提供 C++20 编译器、CMake、OpenGL/Mesa 开发包和图形驱动。
- GLAD：当前仓库已包含 OpenGL core 4.3 loader；若后续改 OpenGL 版本或 extension 集合，需要重新生成。
- SDL3：官方最新稳定版本已经是 SDL3，但本项目当前选择 SDL2，原因是参考项目、ImGui backend 和现有代码计划都以 SDL2 为主。
