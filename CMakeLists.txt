cmake_minimum_required(VERSION 3.24)

project(
    ParallelROAM
    VERSION 0.1.0
    DESCRIPTION "Real-time adaptive terrain subdivision experiments across CPU, data-oriented CPU, and GPU pipelines"
    LANGUAGES C CXX
)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

include(ProjectOptions)
include(CompilerWarnings)
include(Sanitizers)
include(Dependencies)

parallel_roam_apply_global_options()

add_library(parallel_roam_project_options INTERFACE)
target_compile_features(parallel_roam_project_options INTERFACE cxx_std_20)

add_library(parallel_roam_project_warnings INTERFACE)
parallel_roam_set_project_warnings(parallel_roam_project_warnings)

if(PARALLEL_ROAM_ENABLE_SANITIZERS)
    parallel_roam_enable_sanitizers(parallel_roam_project_options)
endif()

if(PARALLEL_ROAM_BUILD_APP)
    parallel_roam_prepare_dependencies()

    set(PARALLEL_ROAM_APP_SOURCES)
    set(PARALLEL_ROAM_FULL_APP_AVAILABLE OFF)

    if(TARGET ParallelROAM::OpenGL AND TARGET ParallelROAM::SDL2 AND TARGET ParallelROAM::GLM AND TARGET ParallelROAM::GLAD AND TARGET ParallelROAM::ImGui AND TARGET ParallelROAM::STB)
        set(PARALLEL_ROAM_FULL_APP_AVAILABLE ON)
        list(
            APPEND
            PARALLEL_ROAM_APP_SOURCES
                src/app/Application.cpp
                src/app/CameraController.cpp
                src/app/InputState.cpp
                src/algorithms/classic_roam/ClassicRoamTerrainLodAlgorithm.cpp
                src/algorithms/classic_roam/ClassicRoamMeshBuilder.cpp
                src/benchmark/TerrainLodBenchmark.cpp
                src/benchmark/RoamProbe.cpp
                src/gui/ImGuiLayer.cpp
                src/platform/Window.cpp
                src/render/Shader.cpp
                src/render/TerrainRenderer.cpp
                src/terrain/HeightMap.cpp
                src/terrain/TerrainMeshBuilder.cpp
        )
        message(STATUS "Full Parallel ROAM application target enabled.")
    else()
        message(STATUS "Full application dependencies are incomplete; building bootstrap target only.")
    endif()

    add_executable(parallel_roam src/main.cpp ${PARALLEL_ROAM_APP_SOURCES})
    add_executable(ParallelROAM::app ALIAS parallel_roam)

    target_include_directories(parallel_roam PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")

    target_link_libraries(
        parallel_roam
        PRIVATE
            parallel_roam_project_options
            parallel_roam_project_warnings
    )

    parallel_roam_link_optional_dependencies(parallel_roam)

    if(PARALLEL_ROAM_FULL_APP_AVAILABLE)
        target_compile_definitions(parallel_roam PRIVATE PARALLEL_ROAM_BUILD_FULL_APP=1)
    endif()

    set_target_properties(
        parallel_roam
        PROPERTIES
            OUTPUT_NAME "ParallelROAM"
            RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    )
endif()

if(PARALLEL_ROAM_BUILD_TESTS)
    enable_testing()

    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/CMakeLists.txt")
        add_subdirectory(tests)
    else()
        message(STATUS "PARALLEL_ROAM_BUILD_TESTS is ON, but tests/CMakeLists.txt does not exist yet.")
    endif()
endif()
