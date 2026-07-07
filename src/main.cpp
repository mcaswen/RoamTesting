#include <iostream>

#if defined(PARALLEL_ROAM_BUILD_FULL_APP)
#include "app/Application.h"
#include "benchmark/RoamProbe.h"
#include "benchmark/TerrainLodBenchmark.h"

#include <string_view>
#endif

#if defined(PARALLEL_ROAM_HAS_SDL2)
#include <SDL.h>
#endif

int main(int argc, char** argv)
{
    (void)argc;
    (void)argv;

#if defined(PARALLEL_ROAM_BUILD_FULL_APP)
    // full app 路径支持无窗口/短窗口入口
    // 参数分流必须早于 Application 初始化
    int maxFrameCount = -1;
    bool fixedFrameSmokeTest = false;
    bool gpuSmokeTest = false;
    bool automaticRuntimeBenchmark = false;
    for (int index = 1; index < argc; ++index)
    {
        // smoke test 用固定帧数退出，避免自动化验证卡在窗口循环里
        if (std::string_view{argv[index]} == "--smoke-test")
        {
            fixedFrameSmokeTest = true;
            maxFrameCount = 3;
        }

        if (std::string_view{argv[index]} == "--gpu-smoke-test")
        {
            gpuSmokeTest = true;
            maxFrameCount = 32;
        }

        if (std::string_view{argv[index]} == "--runtime-benchmark")
        {
            automaticRuntimeBenchmark = true;
        }

        // ROAM 探针不启动窗口，用于快速确认算法层 LOD 是否随相机变化
        if (std::string_view{argv[index]} == "--roam-probe")
        {
            // --roam-probe 保留旧探针命令
            // probe 也必须早于 Application 初始化
            // 它只验证算法层
            return ParallelRoam::Benchmark::RunRoamProbe();
        }

        if (std::string_view{argv[index]} == "--benchmark")
        {
            // --benchmark 走三算法共享 benchmark
            // benchmark 必须在 Application 创建前分流
            return ParallelRoam::Benchmark::RunTerrainLodBenchmarkFromCommandLine(argc, argv);
        }
    }

    if (automaticRuntimeBenchmark && (fixedFrameSmokeTest || gpuSmokeTest))
    {
        std::cerr << "--runtime-benchmark cannot be combined with a smoke-test option.\n";
        return 2;
    }

    ParallelRoam::App::Application application;
    if (gpuSmokeTest)
    {
        application.EnableGpuSmokeTest();
    }
    if (automaticRuntimeBenchmark)
    {
        application.EnableAutomaticRuntimeBenchmark();
    }

    if (!application.Initialize())
    {
        return 1;
    }

    return application.Run(maxFrameCount);
#else
    // 依赖不完整时保留 bootstrap，方便只验证 CMake 和基础链接
    std::cout << "Parallel ROAM bootstrap\n";

#if defined(PARALLEL_ROAM_HAS_OPENGL)
    std::cout << "OpenGL: linked\n";
#else
    std::cout << "OpenGL: not linked\n";
#endif

#if defined(PARALLEL_ROAM_HAS_GLM)
    std::cout << "GLM: linked\n";
#else
    std::cout << "GLM: not linked\n";
#endif

#if defined(PARALLEL_ROAM_HAS_GLAD)
    std::cout << "GLAD: linked\n";
#else
    std::cout << "GLAD: not linked\n";
#endif

#if defined(PARALLEL_ROAM_HAS_STB)
    std::cout << "stb: linked\n";
#else
    std::cout << "stb: not linked\n";
#endif

#if defined(PARALLEL_ROAM_HAS_IMGUI)
    std::cout << "Dear ImGui: linked\n";
#else
    std::cout << "Dear ImGui: not linked\n";
#endif

#if defined(PARALLEL_ROAM_HAS_SDL2)
    SDL_SetMainReady();

    // bootstrap 只初始化 timer subsystem
    // 用于确认 SDL2 链接和基础运行时可用
    if (SDL_Init(SDL_INIT_TIMER) != 0)
    {
        std::cerr << "SDL_Init failed: " << SDL_GetError() << '\n';
        return 1;
    }

    SDL_Quit();
    std::cout << "SDL2: initialized timer subsystem\n";
#else
    std::cout << "SDL2: not linked\n";
#endif

    return 0;
#endif
}
