if(WIN32)
    find_program(PARALLEL_ROAM_POWERSHELL_EXECUTABLE NAMES pwsh powershell)
    if(PARALLEL_ROAM_POWERSHELL_EXECUTABLE)
        add_test(
            NAME cbt_cpp_comment_coverage
            COMMAND
                ${PARALLEL_ROAM_POWERSHELL_EXECUTABLE}
                -NoProfile
                -ExecutionPolicy Bypass
                -File "${CMAKE_CURRENT_SOURCE_DIR}/../tools/check_cbt_comment_coverage.ps1"
                -MinimumPercent 15
        )
    else()
        message(WARNING "PowerShell was not found; CBT comment coverage test is disabled.")
    endif()
endif()

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
