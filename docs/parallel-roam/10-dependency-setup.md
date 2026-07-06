# Dependency Setup

本文档记录 Parallel ROAM 的第三方依赖策略。目标是 Win/mac 都能配置，并且默认 CMake 不偷偷联网。

## 当前依赖

| 依赖 | 用途 | 推荐来源 | FetchContent fallback | 备注 |
|---|---|---|---|---|
| SDL2 | 窗口、输入、OpenGL context | vcpkg / system package | 支持 | 项目当前使用 SDL2，不切 SDL3 |
| OpenGL | 渲染 API | 系统 SDK | 不需要 | macOS 自带 OpenGL 版本可能不足 4.3 |
| GLM | 数学库 | vcpkg / system package | 支持 | header-only |
| GLAD | OpenGL function loader | 本地预生成源码 / vcpkg | 不再默认生成 | 已提交 OpenGL core 4.3 loader |
| stb | Height Map / image loading | vcpkg / system headers | 支持 | header-only |
| Dear ImGui | GUI/debug panels | vcpkg | 支持 | FetchContent 构建 SDL2 + OpenGL3 backend |

## 推荐路径：vcpkg

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

当前 pin 的版本：

```text
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
- SDL2 linked
- GLAD linked from `third_party/glad`
- GLM / stb / Dear ImGui not linked unless installed locally

debug-fetch:
- OpenGL linked
- SDL2 linked
- GLM linked
- GLAD linked from `third_party/glad`
- stb linked
- Dear ImGui linked
```

`debug-vcpkg` 尚未在当前机器验证，因为当前环境没有设置 `VCPKG_ROOT`。

## 平台注意事项

- Windows：推荐 vcpkg manifest，避免手动配置 include/lib path。
- macOS：系统 OpenGL 可能无法满足 OpenGL 4.3 Compute Shader；GPU ROAM-like 阶段需要运行时检查 OpenGL version。
- GLAD：当前仓库已包含 OpenGL core 4.3 loader；若后续改 OpenGL 版本或 extension 集合，需要重新生成。
- SDL3：官方最新稳定版本已经是 SDL3，但本项目当前选择 SDL2，原因是参考项目、ImGui backend 和现有代码计划都以 SDL2 为主。
