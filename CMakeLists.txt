cmake_minimum_required(VERSION 3.24)

project(
    ParallelROAM
    VERSION 0.1.0
    DESCRIPTION "Real-time adaptive terrain subdivision experiments across Classic, data-oriented CPU, and CBT pipelines"
    LANGUAGES C CXX
)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

include(ProjectOptions)
include(CompilerWarnings)
include(Sanitizers)
include(Dependencies)
include(PinnedD3D12Runtime)

parallel_roam_apply_global_options()
find_package(Threads REQUIRED)

string(TOUPPER "${PARALLEL_ROAM_GRAPHICS_API}" PARALLEL_ROAM_GRAPHICS_API_NORMALIZED)
if(NOT PARALLEL_ROAM_GRAPHICS_API_NORMALIZED STREQUAL "OPENGL" AND
   NOT PARALLEL_ROAM_GRAPHICS_API_NORMALIZED STREQUAL "D3D12")
    message(FATAL_ERROR "PARALLEL_ROAM_GRAPHICS_API must be OpenGL or D3D12.")
endif()
set(
    PARALLEL_ROAM_GRAPHICS_API_NORMALIZED
    "${PARALLEL_ROAM_GRAPHICS_API_NORMALIZED}"
    CACHE INTERNAL
    "Normalized graphics backend name."
    FORCE
)
message(STATUS "Parallel ROAM graphics backend: ${PARALLEL_ROAM_GRAPHICS_API}")

add_library(parallel_roam_project_options INTERFACE)
target_compile_features(parallel_roam_project_options INTERFACE cxx_std_20)

if(MSVC)
    target_compile_options(parallel_roam_project_options INTERFACE /utf-8)
endif()

add_library(parallel_roam_project_warnings INTERFACE)
parallel_roam_set_project_warnings(parallel_roam_project_warnings)

if(PARALLEL_ROAM_ENABLE_SANITIZERS)
    parallel_roam_enable_sanitizers(parallel_roam_project_options)
endif()

if(PARALLEL_ROAM_BUILD_APP)
    parallel_roam_prepare_dependencies()

    set(PARALLEL_ROAM_APP_SOURCES src/tools/PerformanceTimer.cpp)
    set(PARALLEL_ROAM_FULL_APP_AVAILABLE OFF)

    if(PARALLEL_ROAM_GRAPHICS_API_NORMALIZED STREQUAL "OPENGL" AND
       TARGET ParallelROAM::OpenGL AND
       TARGET ParallelROAM::SDL2 AND
       TARGET ParallelROAM::GLM AND
       TARGET ParallelROAM::GLAD AND
       TARGET ParallelROAM::ImGui AND
       TARGET ParallelROAM::STB)
        set(PARALLEL_ROAM_FULL_APP_AVAILABLE ON)
        list(
            APPEND
            PARALLEL_ROAM_APP_SOURCES
                src/app/Application.cpp
                src/app/CameraController.cpp
                src/app/InputState.cpp
                src/app/RuntimeBenchmark.cpp
                src/algorithms/TerrainLodView.cpp
                src/algorithms/cbt_2024/Cbt2024Support.cpp
                src/algorithms/cbt_2024/CbtBisectCommit.cpp
                src/algorithms/cbt_2024/CbtBisectorTopology.cpp
                src/algorithms/cbt_2024/CbtClassification.cpp
                src/algorithms/cbt_2024/CbtOccupancyTree.cpp
                src/algorithms/cbt_2024/CbtSplitPlanner.cpp
                src/algorithms/classic_roam/ClassicRoamTerrainLodAlgorithm.cpp
                src/algorithms/classic_roam/ClassicRoamMeshBuilder.cpp
                src/algorithms/classic_roam/ClassicRoamMeshEmit.cpp
                src/algorithms/classic_roam/ClassicRoamQueues.cpp
                src/algorithms/classic_roam/ClassicRoamScoring.cpp
                src/algorithms/classic_roam/ClassicRoamState.cpp
                src/algorithms/classic_roam/ClassicRoamTopology.cpp
                src/algorithms/classic_roam/ClassicRoamValidation.cpp
                src/algorithms/data_oriented_roam/DataOrientedRoamTerrainLodAlgorithm.cpp
                src/algorithms/data_oriented_roam/DataOrientedRoamCandidateMarking.cpp
                src/algorithms/data_oriented_roam/DataOrientedRoamPipeline.cpp
                src/algorithms/data_oriented_roam/DataOrientedRoamMeshEmit.cpp
                src/algorithms/data_oriented_roam/DataOrientedRoamQueues.cpp
                src/algorithms/data_oriented_roam/DataOrientedRoamScoring.cpp
                src/algorithms/data_oriented_roam/DataOrientedRoamState.cpp
                src/algorithms/data_oriented_roam/DataOrientedRoamThreadPool.cpp
                src/algorithms/data_oriented_roam/DataOrientedRoamTopology.cpp
                src/algorithms/data_oriented_roam/DataOrientedRoamValidation.cpp
                src/benchmark/TerrainLodBenchmark.cpp
                src/benchmark/RoamProbe.cpp
                src/gui/ImGuiLayer.cpp
                src/platform/Window.cpp
                src/render/Shader.cpp
                src/render/GraphicsBackend.cpp
                src/render/OpenGlGraphicsBackend.cpp
                src/render/TerrainRenderer.cpp
                src/terrain/HeightMap.cpp
                src/terrain/TerrainMeshBuilder.cpp
        )
        message(STATUS "Full Parallel ROAM application target enabled.")
    elseif(PARALLEL_ROAM_GRAPHICS_API_NORMALIZED STREQUAL "D3D12" AND
           TARGET ParallelROAM::D3D12 AND
           TARGET ParallelROAM::SDL2 AND
           TARGET ParallelROAM::GLM AND
           TARGET ParallelROAM::ImGui AND
           TARGET ParallelROAM::STB)
        set(PARALLEL_ROAM_FULL_APP_AVAILABLE ON)
        list(
            APPEND
            PARALLEL_ROAM_APP_SOURCES
                src/app/Application.cpp
                src/app/CameraController.cpp
                src/app/InputState.cpp
                src/app/RuntimeBenchmark.cpp
                src/algorithms/TerrainLodView.cpp
                src/algorithms/cbt_2024/Cbt2024Support.cpp
                src/algorithms/cbt_2024/CbtBisectCommit.cpp
                src/algorithms/cbt_2024/CbtBisectorTopology.cpp
                src/algorithms/cbt_2024/CbtClassification.cpp
                src/algorithms/cbt_2024/CbtOccupancyTree.cpp
                src/algorithms/cbt_2024/CbtSplitPlanner.cpp
                src/algorithms/cbt_2024/d3d12/D3D12CbtGpuState.cpp
                src/algorithms/cbt_2024/d3d12/D3D12CbtDiagnostics.cpp
                src/algorithms/cbt_2024/d3d12/D3D12CbtGeometryPipeline.cpp
                src/algorithms/cbt_2024/d3d12/D3D12CbtFramePipeline.cpp
                src/algorithms/cbt_2024/d3d12/D3D12CbtOccupancyTree.cpp
                src/algorithms/cbt_2024/d3d12/D3D12CbtTerrainLodAlgorithm.cpp
                src/algorithms/classic_roam/ClassicRoamTerrainLodAlgorithm.cpp
                src/algorithms/classic_roam/ClassicRoamMeshBuilder.cpp
                src/algorithms/classic_roam/ClassicRoamMeshEmit.cpp
                src/algorithms/classic_roam/ClassicRoamQueues.cpp
                src/algorithms/classic_roam/ClassicRoamScoring.cpp
                src/algorithms/classic_roam/ClassicRoamState.cpp
                src/algorithms/classic_roam/ClassicRoamTopology.cpp
                src/algorithms/classic_roam/ClassicRoamValidation.cpp
                src/algorithms/data_oriented_roam/DataOrientedRoamTerrainLodAlgorithm.cpp
                src/algorithms/data_oriented_roam/DataOrientedRoamCandidateMarking.cpp
                src/algorithms/data_oriented_roam/DataOrientedRoamPipeline.cpp
                src/algorithms/data_oriented_roam/DataOrientedRoamMeshEmit.cpp
                src/algorithms/data_oriented_roam/DataOrientedRoamQueues.cpp
                src/algorithms/data_oriented_roam/DataOrientedRoamScoring.cpp
                src/algorithms/data_oriented_roam/DataOrientedRoamState.cpp
                src/algorithms/data_oriented_roam/DataOrientedRoamThreadPool.cpp
                src/algorithms/data_oriented_roam/DataOrientedRoamTopology.cpp
                src/algorithms/data_oriented_roam/DataOrientedRoamValidation.cpp
                src/benchmark/TerrainLodBenchmark.cpp
                src/benchmark/RoamProbe.cpp
                src/gui/ImGuiLayer.cpp
                src/platform/Window.cpp
                src/render/GraphicsBackend.cpp
                src/render/D3D12AgilitySdkExports.cpp
                src/render/D3D12GraphicsBackend.cpp
                src/render/D3D12ProceduralTerrainPipeline.cpp
                src/render/D3D12TerrainRenderer.cpp
                src/terrain/HeightMap.cpp
                src/terrain/TerrainMeshBuilder.cpp
        )
        message(STATUS "Full Parallel ROAM D3D12 application target enabled.")
    else()
        message(STATUS "Full application dependencies are incomplete; building bootstrap target only.")
    endif()

    add_executable(parallel_roam src/main.cpp ${PARALLEL_ROAM_APP_SOURCES})
    add_executable(ParallelROAM::app ALIAS parallel_roam)

    target_include_directories(
        parallel_roam
        PRIVATE
            "${CMAKE_CURRENT_SOURCE_DIR}/src"
    )
    target_compile_definitions(parallel_roam PRIVATE PARALLEL_ROAM_BUILD_CONFIG="$<CONFIG>")

    if(PARALLEL_ROAM_GRAPHICS_API_NORMALIZED STREQUAL "OPENGL")
        target_compile_definitions(parallel_roam PRIVATE PARALLEL_ROAM_GRAPHICS_API_OPENGL=1)
    else()
        target_compile_definitions(parallel_roam PRIVATE PARALLEL_ROAM_GRAPHICS_API_D3D12=1)
    endif()

    target_link_libraries(
        parallel_roam
        PRIVATE
            parallel_roam_project_options
            parallel_roam_project_warnings
            Threads::Threads
    )

    parallel_roam_link_optional_dependencies(parallel_roam)

    if(PARALLEL_ROAM_FULL_APP_AVAILABLE)
        target_compile_definitions(parallel_roam PRIVATE PARALLEL_ROAM_BUILD_FULL_APP=1)
    endif()

    if(PARALLEL_ROAM_FULL_APP_AVAILABLE AND PARALLEL_ROAM_GRAPHICS_API_NORMALIZED STREQUAL "D3D12")
        parallel_roam_configure_pinned_d3d12_runtime(parallel_roam PARALLEL_ROAM_DXC_EXECUTABLE)

        set(PARALLEL_ROAM_DX12_SHADER_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated/shaders")
        set(PARALLEL_ROAM_DX12_VERTEX_SHADER "${PARALLEL_ROAM_DX12_SHADER_DIR}/TerrainVS.cso")
        set(PARALLEL_ROAM_DX12_PIXEL_SHADER "${PARALLEL_ROAM_DX12_SHADER_DIR}/TerrainPS.cso")
        set(
            PARALLEL_ROAM_DX12_CBT_PROCEDURAL_VERTEX_SHADER
            "${PARALLEL_ROAM_DX12_SHADER_DIR}/CbtProceduralTerrainVS.cso")
        set(PARALLEL_ROAM_DX12_CBT_BOOTSTRAP_INDEXATION_SHADER "${PARALLEL_ROAM_DX12_SHADER_DIR}/CbtBootstrapIndexation.cso")
        set(PARALLEL_ROAM_DX12_CBT_BOOTSTRAP_PREPARE_SHADER "${PARALLEL_ROAM_DX12_SHADER_DIR}/CbtBootstrapPrepareIndirect.cso")
        set(PARALLEL_ROAM_DX12_CBT_ACTIVE_GEOMETRY_SHADER "${PARALLEL_ROAM_DX12_SHADER_DIR}/CbtBootstrapBuildActiveGeometry.cso")
        set(PARALLEL_ROAM_DX12_CBT_MODIFIED_GEOMETRY_SHADER "${PARALLEL_ROAM_DX12_SHADER_DIR}/CbtBootstrapBuildModifiedGeometry.cso")
        target_compile_definitions(
            parallel_roam
            PRIVATE PARALLEL_ROAM_DX12_SHADER_DIR="${PARALLEL_ROAM_DX12_SHADER_DIR}")
        add_custom_command(
            OUTPUT
                ${PARALLEL_ROAM_DX12_VERTEX_SHADER}
                ${PARALLEL_ROAM_DX12_PIXEL_SHADER}
                ${PARALLEL_ROAM_DX12_CBT_PROCEDURAL_VERTEX_SHADER}
                ${PARALLEL_ROAM_DX12_CBT_BOOTSTRAP_INDEXATION_SHADER}
                ${PARALLEL_ROAM_DX12_CBT_BOOTSTRAP_PREPARE_SHADER}
                ${PARALLEL_ROAM_DX12_CBT_ACTIVE_GEOMETRY_SHADER}
                ${PARALLEL_ROAM_DX12_CBT_MODIFIED_GEOMETRY_SHADER}
            COMMAND ${CMAKE_COMMAND} -E make_directory "${PARALLEL_ROAM_DX12_SHADER_DIR}"
            COMMAND ${PARALLEL_ROAM_DXC_EXECUTABLE}
                -T vs_6_0 -E VSMain -Fo ${PARALLEL_ROAM_DX12_VERTEX_SHADER}
                "${CMAKE_CURRENT_SOURCE_DIR}/assets/shaders/dx12/Terrain.hlsl"
            COMMAND ${PARALLEL_ROAM_DXC_EXECUTABLE}
                -T ps_6_0 -E PSMain -Fo ${PARALLEL_ROAM_DX12_PIXEL_SHADER}
                "${CMAKE_CURRENT_SOURCE_DIR}/assets/shaders/dx12/Terrain.hlsl"
            COMMAND ${PARALLEL_ROAM_DXC_EXECUTABLE}
                -T vs_6_6 -E VSCbtProcedural -Fo ${PARALLEL_ROAM_DX12_CBT_PROCEDURAL_VERTEX_SHADER}
                "${CMAKE_CURRENT_SOURCE_DIR}/assets/shaders/dx12/CbtProceduralTerrain.hlsl"
            COMMAND ${PARALLEL_ROAM_DXC_EXECUTABLE}
                -T cs_6_6 -E CSIndexation
                -I "${CMAKE_CURRENT_SOURCE_DIR}/src/algorithms/cbt_2024"
                -Fo ${PARALLEL_ROAM_DX12_CBT_BOOTSTRAP_INDEXATION_SHADER}
                "${CMAKE_CURRENT_SOURCE_DIR}/assets/shaders/dx12/cbt/CbtBootstrap.hlsl"
            COMMAND ${PARALLEL_ROAM_DXC_EXECUTABLE}
                -T cs_6_6 -E CSPrepareIndirect
                -I "${CMAKE_CURRENT_SOURCE_DIR}/src/algorithms/cbt_2024"
                -Fo ${PARALLEL_ROAM_DX12_CBT_BOOTSTRAP_PREPARE_SHADER}
                "${CMAKE_CURRENT_SOURCE_DIR}/assets/shaders/dx12/cbt/CbtBootstrap.hlsl"
            COMMAND ${PARALLEL_ROAM_DXC_EXECUTABLE}
                -T cs_6_6 -E CSBuildActiveGeometry
                -I "${CMAKE_CURRENT_SOURCE_DIR}/src/algorithms/cbt_2024"
                -Fo ${PARALLEL_ROAM_DX12_CBT_ACTIVE_GEOMETRY_SHADER}
                "${CMAKE_CURRENT_SOURCE_DIR}/assets/shaders/dx12/cbt/CbtBootstrap.hlsl"
            COMMAND ${PARALLEL_ROAM_DXC_EXECUTABLE}
                -T cs_6_6 -E CSBuildModifiedGeometry
                -I "${CMAKE_CURRENT_SOURCE_DIR}/src/algorithms/cbt_2024"
                -Fo ${PARALLEL_ROAM_DX12_CBT_MODIFIED_GEOMETRY_SHADER}
                "${CMAKE_CURRENT_SOURCE_DIR}/assets/shaders/dx12/cbt/CbtBootstrap.hlsl"
            DEPENDS
                "${CMAKE_CURRENT_SOURCE_DIR}/assets/shaders/dx12/Terrain.hlsl"
                "${CMAKE_CURRENT_SOURCE_DIR}/assets/shaders/dx12/CbtProceduralTerrain.hlsl"
                "${CMAKE_CURRENT_SOURCE_DIR}/assets/shaders/dx12/cbt/CbtBootstrap.hlsl"
                "${CMAKE_CURRENT_SOURCE_DIR}/assets/shaders/dx12/cbt/CbtGpuAbi.hlsli"
                "${CMAKE_CURRENT_SOURCE_DIR}/src/algorithms/cbt_2024/CbtGpuAbi.shared.h"
            VERBATIM
        )
        add_custom_target(parallel_roam_dx12_shaders
            DEPENDS
                ${PARALLEL_ROAM_DX12_VERTEX_SHADER}
                ${PARALLEL_ROAM_DX12_PIXEL_SHADER}
                ${PARALLEL_ROAM_DX12_CBT_PROCEDURAL_VERTEX_SHADER}
                ${PARALLEL_ROAM_DX12_CBT_BOOTSTRAP_INDEXATION_SHADER}
                ${PARALLEL_ROAM_DX12_CBT_BOOTSTRAP_PREPARE_SHADER}
                ${PARALLEL_ROAM_DX12_CBT_ACTIVE_GEOMETRY_SHADER}
                ${PARALLEL_ROAM_DX12_CBT_MODIFIED_GEOMETRY_SHADER})

        set(PARALLEL_ROAM_CBT_OCBT_CAPACITY_NAMES 128K 256K 512K 1M)
        set(PARALLEL_ROAM_CBT_OCBT_CAPACITY_VALUES 131072 262144 524288 1048576)
        set(
            PARALLEL_ROAM_CBT_OCBT_ENTRIES
            CSClear
            CSApplyUpdates
            CSReducePre
            CSReduceFirst
            CSReduceSecond
            CSDecodeOccupied
            CSDecodeFree)
        set(
            PARALLEL_ROAM_CBT_OCBT_SUFFIXES
            Clear
            ApplyUpdates
            ReducePre
            ReduceFirst
            ReduceSecond
            DecodeOccupied
            DecodeFree)
        set(PARALLEL_ROAM_DX12_CBT_OCBT_SHADERS)
        foreach(PARALLEL_ROAM_CBT_CAPACITY_INDEX RANGE 0 3)
            list(
                GET
                PARALLEL_ROAM_CBT_OCBT_CAPACITY_NAMES
                ${PARALLEL_ROAM_CBT_CAPACITY_INDEX}
                PARALLEL_ROAM_CBT_CAPACITY_NAME)
            list(
                GET
                PARALLEL_ROAM_CBT_OCBT_CAPACITY_VALUES
                ${PARALLEL_ROAM_CBT_CAPACITY_INDEX}
                PARALLEL_ROAM_CBT_CAPACITY_VALUE)
            foreach(PARALLEL_ROAM_CBT_ENTRY_INDEX RANGE 0 6)
                list(
                    GET
                    PARALLEL_ROAM_CBT_OCBT_ENTRIES
                    ${PARALLEL_ROAM_CBT_ENTRY_INDEX}
                    PARALLEL_ROAM_CBT_ENTRY)
                list(
                    GET
                    PARALLEL_ROAM_CBT_OCBT_SUFFIXES
                    ${PARALLEL_ROAM_CBT_ENTRY_INDEX}
                    PARALLEL_ROAM_CBT_SUFFIX)
                set(
                    PARALLEL_ROAM_CBT_OCBT_SHADER
                    "${PARALLEL_ROAM_DX12_SHADER_DIR}/CbtOcbt${PARALLEL_ROAM_CBT_CAPACITY_NAME}${PARALLEL_ROAM_CBT_SUFFIX}.cso")
                list(APPEND PARALLEL_ROAM_DX12_CBT_OCBT_SHADERS ${PARALLEL_ROAM_CBT_OCBT_SHADER})
                add_custom_command(
                    OUTPUT ${PARALLEL_ROAM_CBT_OCBT_SHADER}
                    COMMAND ${PARALLEL_ROAM_DXC_EXECUTABLE}
                        -T cs_6_6
                        -E ${PARALLEL_ROAM_CBT_ENTRY}
                        -D CBT_CAPACITY=${PARALLEL_ROAM_CBT_CAPACITY_VALUE}
                        -I "${CMAKE_CURRENT_SOURCE_DIR}/assets/shaders/dx12/cbt"
                        -Fo ${PARALLEL_ROAM_CBT_OCBT_SHADER}
                        "${CMAKE_CURRENT_SOURCE_DIR}/assets/shaders/dx12/cbt/CbtOccupancyTreeTests.hlsl"
                    DEPENDS
                        "${CMAKE_CURRENT_SOURCE_DIR}/assets/shaders/dx12/cbt/CbtOccupancyTree.hlsli"
                        "${CMAKE_CURRENT_SOURCE_DIR}/assets/shaders/dx12/cbt/CbtOccupancyTreeTests.hlsl"
                    VERBATIM)
            endforeach()
        endforeach()
        add_custom_target(
            parallel_roam_dx12_cbt_ocbt_shaders
            DEPENDS ${PARALLEL_ROAM_DX12_CBT_OCBT_SHADERS})

        set(PARALLEL_ROAM_CBT_TOPOLOGY_ENTRIES CSResetE0 CSClassify CSPrepareClassificationIndirect CSSplitE2 CSPrepareAllocationIndirect CSAllocateE2 CSBisectE3 CSPreparePropagationIndirectE3 CSPropagateBisectE3 CSReducePre CSReduceFirst CSReduceSecond CSValidateE3)
        set(PARALLEL_ROAM_CBT_TOPOLOGY_SUFFIXES ResetE0 Classify PrepareClassificationIndirect SplitE2 PrepareAllocationIndirect AllocateE2 BisectE3 PreparePropagationIndirectE3 PropagateBisectE3 ReducePre ReduceFirst ReduceSecond ValidateE3)
        set(PARALLEL_ROAM_DX12_CBT_TOPOLOGY_SHADERS)
        foreach(PARALLEL_ROAM_CBT_CAPACITY_INDEX RANGE 0 3)
            list(GET PARALLEL_ROAM_CBT_OCBT_CAPACITY_NAMES ${PARALLEL_ROAM_CBT_CAPACITY_INDEX} PARALLEL_ROAM_CBT_CAPACITY_NAME)
            list(GET PARALLEL_ROAM_CBT_OCBT_CAPACITY_VALUES ${PARALLEL_ROAM_CBT_CAPACITY_INDEX} PARALLEL_ROAM_CBT_CAPACITY_VALUE)
            foreach(PARALLEL_ROAM_CBT_ENTRY_INDEX RANGE 0 12)
                list(GET PARALLEL_ROAM_CBT_TOPOLOGY_ENTRIES ${PARALLEL_ROAM_CBT_ENTRY_INDEX} PARALLEL_ROAM_CBT_ENTRY)
                list(GET PARALLEL_ROAM_CBT_TOPOLOGY_SUFFIXES ${PARALLEL_ROAM_CBT_ENTRY_INDEX} PARALLEL_ROAM_CBT_SUFFIX)
                set(PARALLEL_ROAM_CBT_TOPOLOGY_SHADER
                    "${PARALLEL_ROAM_DX12_SHADER_DIR}/CbtTopology${PARALLEL_ROAM_CBT_CAPACITY_NAME}${PARALLEL_ROAM_CBT_SUFFIX}.cso")
                list(APPEND PARALLEL_ROAM_DX12_CBT_TOPOLOGY_SHADERS ${PARALLEL_ROAM_CBT_TOPOLOGY_SHADER})
                add_custom_command(
                    OUTPUT ${PARALLEL_ROAM_CBT_TOPOLOGY_SHADER}
                    COMMAND ${PARALLEL_ROAM_DXC_EXECUTABLE}
                        -T cs_6_6
                        -E ${PARALLEL_ROAM_CBT_ENTRY}
                        -D CBT_CAPACITY=${PARALLEL_ROAM_CBT_CAPACITY_VALUE}
                        -I "${CMAKE_CURRENT_SOURCE_DIR}/assets/shaders/dx12/cbt"
                        -I "${CMAKE_CURRENT_SOURCE_DIR}/src/algorithms/cbt_2024"
                        -Fo ${PARALLEL_ROAM_CBT_TOPOLOGY_SHADER}
                        "${CMAKE_CURRENT_SOURCE_DIR}/assets/shaders/dx12/cbt/CbtTopologyE0.hlsl"
                    DEPENDS
                        "${CMAKE_CURRENT_SOURCE_DIR}/assets/shaders/dx12/cbt/CbtOccupancyTree.hlsli"
                        "${CMAKE_CURRENT_SOURCE_DIR}/assets/shaders/dx12/cbt/CbtGpuAbi.hlsli"
                        "${CMAKE_CURRENT_SOURCE_DIR}/src/algorithms/cbt_2024/CbtGpuAbi.shared.h"
                        "${CMAKE_CURRENT_SOURCE_DIR}/assets/shaders/dx12/cbt/CbtTopologyE0.hlsl"
                    VERBATIM)
            endforeach()
        endforeach()
        add_custom_target(
            parallel_roam_dx12_cbt_topology_shaders
            DEPENDS ${PARALLEL_ROAM_DX12_CBT_TOPOLOGY_SHADERS})
        add_dependencies(
            parallel_roam
            parallel_roam_dx12_shaders
            parallel_roam_dx12_cbt_ocbt_shaders
            parallel_roam_dx12_cbt_topology_shaders)
        add_custom_command(
            TARGET parallel_roam
            POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:parallel_roam>/assets/shaders/dx12"
            COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${PARALLEL_ROAM_DX12_SHADER_DIR}"
                "$<TARGET_FILE_DIR:parallel_roam>/assets/shaders/dx12"
            COMMENT "Copying compiled D3D12 shaders next to ParallelROAM"
        )
    endif()

    set_target_properties(
        parallel_roam
        PROPERTIES
            OUTPUT_NAME "ParallelROAM"
            RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    )

    foreach(config IN ITEMS Debug Release RelWithDebInfo MinSizeRel)
        string(TOUPPER "${config}" config_upper)
        set_target_properties(
            parallel_roam
            PROPERTIES
                "RUNTIME_OUTPUT_DIRECTORY_${config_upper}" "${CMAKE_BINARY_DIR}/bin"
        )
    endforeach()

    add_custom_command(
        TARGET parallel_roam
        POST_BUILD
        COMMAND
            ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_CURRENT_SOURCE_DIR}/assets"
            "$<TARGET_FILE_DIR:parallel_roam>/assets"
        COMMENT "Copying runtime assets next to ParallelROAM"
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
