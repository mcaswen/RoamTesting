if(WIN32)
    find_program(PARALLEL_ROAM_POWERSHELL_EXECUTABLE NAMES pwsh powershell)
    if(PARALLEL_ROAM_POWERSHELL_EXECUTABLE)
        add_test(
            NAME cpp_comment_coverage
            COMMAND
                ${PARALLEL_ROAM_POWERSHELL_EXECUTABLE}
                -NoProfile
                -ExecutionPolicy Bypass
                -File "${CMAKE_CURRENT_SOURCE_DIR}/../tools/check_cpp_comment_coverage.ps1"
        )
    else()
        message(WARNING "PowerShell was not found; C++ comment coverage test is disabled.")
    endif()
endif()

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

if(TARGET parallel_roam)
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
    set_tests_properties(
        roam_budget_reentry_classic
        roam_budget_reentry_dod
        PROPERTIES WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/.."
    )
endif()
