if(WIN32)
    find_program(PARALLEL_ROAM_POWERSHELL_EXECUTABLE NAMES pwsh powershell)
    if(PARALLEL_ROAM_POWERSHELL_EXECUTABLE)
        add_test(
            NAME source_comment_coverage
            COMMAND
                ${PARALLEL_ROAM_POWERSHELL_EXECUTABLE}
                -NoProfile
                -ExecutionPolicy Bypass
                -File "${CMAKE_CURRENT_SOURCE_DIR}/../tools/check_source_comment_coverage.ps1"
        )
    else()
        message(WARNING "PowerShell was not found; C++/HLSL comment coverage test is disabled.")
    endif()
endif()

add_executable(
    parallel_roam_performance_timer_tests
    PerformanceTimerTests.cpp
    ../src/tools/PerformanceTimer.cpp
)

target_include_directories(
    parallel_roam_performance_timer_tests
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../src"
)

target_link_libraries(
    parallel_roam_performance_timer_tests
    PRIVATE
        parallel_roam_project_options
        parallel_roam_project_warnings
        Threads::Threads
)

add_test(NAME performance_timer COMMAND parallel_roam_performance_timer_tests)

add_executable(
    parallel_roam_cbt_occupancy_tree_tests
    CbtOccupancyTreeTests.cpp
    ../src/algorithms/cbt_2024/CbtOccupancyTree.cpp
)

target_include_directories(
    parallel_roam_cbt_occupancy_tree_tests
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../src"
)

target_link_libraries(
    parallel_roam_cbt_occupancy_tree_tests
    PRIVATE
        parallel_roam_project_options
        parallel_roam_project_warnings
)

add_test(NAME cbt_occupancy_tree COMMAND parallel_roam_cbt_occupancy_tree_tests)
set_tests_properties(cbt_occupancy_tree PROPERTIES LABELS "unit;cbt" TIMEOUT 30)

add_executable(
    parallel_roam_cbt_bisector_topology_tests
    CbtBisectorTopologyTests.cpp
    ../src/algorithms/cbt_2024/CbtBisectorTopology.cpp
    ../src/algorithms/cbt_2024/CbtOccupancyTree.cpp
)

target_include_directories(
    parallel_roam_cbt_bisector_topology_tests
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../src"
)

target_link_libraries(
    parallel_roam_cbt_bisector_topology_tests
    PRIVATE
        parallel_roam_project_options
        parallel_roam_project_warnings
)

add_test(NAME cbt_bisector_topology COMMAND parallel_roam_cbt_bisector_topology_tests)
set_tests_properties(cbt_bisector_topology PROPERTIES LABELS "unit;cbt" TIMEOUT 30)

add_executable(
    parallel_roam_nested_wedgie_tests
    RoamNestedWedgieTests.cpp
)

target_include_directories(
    parallel_roam_nested_wedgie_tests
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../src"
)

target_link_libraries(
    parallel_roam_nested_wedgie_tests
    PRIVATE
        parallel_roam_project_options
        parallel_roam_project_warnings
)

add_test(NAME roam_nested_wedgie COMMAND parallel_roam_nested_wedgie_tests)

if(NOT TARGET ParallelROAM::GLM)
    message(STATUS "Skipping terrain LOD view tests because GLM is unavailable.")
    return()
endif()

add_executable(
    parallel_roam_screen_projection_tests
    RoamScreenProjectionTests.cpp
    ../src/algorithms/TerrainLodView.cpp
)

target_include_directories(
    parallel_roam_screen_projection_tests
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../src"
)

target_link_libraries(
    parallel_roam_screen_projection_tests
    PRIVATE
        parallel_roam_project_options
        parallel_roam_project_warnings
        ParallelROAM::GLM
)

add_test(NAME roam_screen_projection COMMAND parallel_roam_screen_projection_tests)

add_executable(
    parallel_roam_terrain_lod_view_tests
    TerrainLodViewTests.cpp
    ../src/algorithms/TerrainLodView.cpp
)

target_include_directories(
    parallel_roam_terrain_lod_view_tests
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../src"
)

target_link_libraries(
    parallel_roam_terrain_lod_view_tests
    PRIVATE
        parallel_roam_project_options
        parallel_roam_project_warnings
        ParallelROAM::GLM
)

add_test(NAME terrain_lod_view COMMAND parallel_roam_terrain_lod_view_tests)

add_executable(
    parallel_roam_terrain_lod_contract_tests
    TerrainLodContractTests.cpp
)

target_include_directories(
    parallel_roam_terrain_lod_contract_tests
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../src"
)

target_link_libraries(
    parallel_roam_terrain_lod_contract_tests
    PRIVATE
        parallel_roam_project_options
        parallel_roam_project_warnings
        ParallelROAM::GLM
)

add_test(NAME terrain_lod_contract COMMAND parallel_roam_terrain_lod_contract_tests)
set_tests_properties(terrain_lod_contract PROPERTIES LABELS "unit;cbt" TIMEOUT 30)

add_executable(
    parallel_roam_cbt_classification_tests
    CbtClassificationTests.cpp
    ../src/algorithms/TerrainLodView.cpp
    ../src/algorithms/cbt_2024/CbtClassification.cpp
    ../src/algorithms/cbt_2024/CbtBisectorTopology.cpp
    ../src/algorithms/cbt_2024/CbtOccupancyTree.cpp
)

target_include_directories(
    parallel_roam_cbt_classification_tests
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../src"
)

target_link_libraries(
    parallel_roam_cbt_classification_tests
    PRIVATE
        parallel_roam_project_options
        parallel_roam_project_warnings
        ParallelROAM::GLM
)

add_test(NAME cbt_classification COMMAND parallel_roam_cbt_classification_tests)
set_tests_properties(cbt_classification PROPERTIES LABELS "unit;cbt" TIMEOUT 30)

add_executable(
    parallel_roam_cbt_split_planner_tests
    CbtSplitPlannerTests.cpp
    ../src/algorithms/cbt_2024/CbtSplitPlanner.cpp
    ../src/algorithms/cbt_2024/CbtBisectorTopology.cpp
    ../src/algorithms/cbt_2024/CbtOccupancyTree.cpp
)

target_include_directories(
    parallel_roam_cbt_split_planner_tests
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../src"
)

target_link_libraries(
    parallel_roam_cbt_split_planner_tests
    PRIVATE
        parallel_roam_project_options
        parallel_roam_project_warnings
)

add_test(NAME cbt_split_planner COMMAND parallel_roam_cbt_split_planner_tests)
set_tests_properties(cbt_split_planner PROPERTIES LABELS "unit;cbt" TIMEOUT 30)

add_executable(
    parallel_roam_cbt_bisect_commit_tests
    CbtBisectCommitTests.cpp
    ../src/algorithms/cbt_2024/CbtBisectCommit.cpp
    ../src/algorithms/cbt_2024/CbtBisectorTopology.cpp
    ../src/algorithms/cbt_2024/CbtOccupancyTree.cpp
)

target_include_directories(
    parallel_roam_cbt_bisect_commit_tests
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../src"
)

target_link_libraries(
    parallel_roam_cbt_bisect_commit_tests
    PRIVATE
        parallel_roam_project_options
        parallel_roam_project_warnings
)

add_test(NAME cbt_bisect_commit COMMAND parallel_roam_cbt_bisect_commit_tests)
set_tests_properties(cbt_bisect_commit PROPERTIES LABELS "unit;cbt" TIMEOUT 30)

add_executable(
    parallel_roam_cbt_simplify_commit_tests
    CbtSimplifyCommitTests.cpp
    ../src/algorithms/cbt_2024/CbtBisectCommit.cpp
    ../src/algorithms/cbt_2024/CbtSimplifyCommit.cpp
    ../src/algorithms/cbt_2024/CbtBisectorTopology.cpp
    ../src/algorithms/cbt_2024/CbtOccupancyTree.cpp
)

target_include_directories(
    parallel_roam_cbt_simplify_commit_tests
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../src"
)

target_link_libraries(
    parallel_roam_cbt_simplify_commit_tests
    PRIVATE
        parallel_roam_project_options
        parallel_roam_project_warnings
)

add_test(NAME cbt_simplify_commit COMMAND parallel_roam_cbt_simplify_commit_tests)
set_tests_properties(cbt_simplify_commit PROPERTIES LABELS "unit;cbt" TIMEOUT 30)

if(TARGET parallel_roam)
    if(WIN32 AND PARALLEL_ROAM_GRAPHICS_API_NORMALIZED STREQUAL "D3D12")
        add_test(
            NAME cbt_procedural_e3_quick
            COMMAND $<TARGET_FILE:parallel_roam> --cbt-procedural-smoke-test --cbt-capacity 128K
        )
        foreach(CBT_CAPACITY IN ITEMS 256K 512K 1M)
            string(TOLOWER "${CBT_CAPACITY}" CBT_CAPACITY_TEST_SUFFIX)
            add_test(
                NAME cbt_procedural_e3_${CBT_CAPACITY_TEST_SUFFIX}
                COMMAND
                    $<TARGET_FILE:parallel_roam>
                    --cbt-procedural-smoke-test
                    --cbt-capacity
                    ${CBT_CAPACITY}
            )
            set_tests_properties(
                cbt_procedural_e3_${CBT_CAPACITY_TEST_SUFFIX}
                PROPERTIES
                    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/.."
                    LABELS "gpu-quick;cbt;integration;capacity"
                    TIMEOUT 90
            )
        endforeach()
        set_tests_properties(
            cbt_procedural_e3_quick
            PROPERTIES
                WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/.."
                LABELS "gpu-quick;cbt;integration"
                TIMEOUT 90
        )
    endif()

    add_test(
        NAME roam_budget_reentry_classic
        COMMAND
            $<TARGET_FILE:parallel_roam>
            --benchmark
            --algorithm classic
            --profile budget-reentry
    )
    add_test(
        NAME roam_budget_reentry_dod
        COMMAND
            $<TARGET_FILE:parallel_roam>
            --benchmark
            --algorithm dod
            --profile budget-reentry
    )
    add_test(
        NAME roam_split_queue_view_dod
        COMMAND
            $<TARGET_FILE:parallel_roam>
            --benchmark
            --algorithm dod
            --profile smoke
    )
    add_test(
        NAME roam_incremental_mesh_emit_classic
        COMMAND
            $<TARGET_FILE:parallel_roam>
            --benchmark
            --algorithm classic
            --profile incremental-emit
    )
    add_test(
        NAME roam_incremental_mesh_emit_dod
        COMMAND
            $<TARGET_FILE:parallel_roam>
            --benchmark
            --algorithm dod
            --profile incremental-emit
    )
    set_tests_properties(
        roam_budget_reentry_classic
        roam_budget_reentry_dod
        roam_split_queue_view_dod
        roam_incremental_mesh_emit_classic
        roam_incremental_mesh_emit_dod
        PROPERTIES WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/.."
    )
endif()
