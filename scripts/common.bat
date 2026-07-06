@echo off
setlocal EnableExtensions EnableDelayedExpansion

if "%~1"=="" (
    echo [ParallelROAM] missing CMake preset
    exit /b 1
)

set "PRESET=%~1"
shift /1
set "RUN_ARGS="

:collect_args
if "%~1"=="" goto :args_done
set "RUN_ARGS=!RUN_ARGS! "%~1""
shift /1
goto :collect_args

:args_done

set "SCRIPT_DIR=%~dp0"
for %%I in ("%SCRIPT_DIR%..") do set "PROJECT_ROOT=%%~fI"
set "PORTABLE_CMAKE_EXE=%PROJECT_ROOT%\tools\cmake\bin\cmake.exe"
set "CMAKE_EXE=%PORTABLE_CMAKE_EXE%"

pushd "%PROJECT_ROOT%" || exit /b 1

echo [ParallelROAM] configure: %PRESET%
if exist "%PORTABLE_CMAKE_EXE%" goto :cmake_ready

cmake --version >nul 2>nul
if not errorlevel 1 (
    set "CMAKE_EXE=cmake"
    goto :cmake_ready
)

echo [ParallelROAM] CMake not found; downloading portable CMake into tools\cmake.
call "%SCRIPT_DIR%setup_portable_cmake.bat"
if errorlevel 1 goto :fail
if not exist "%PORTABLE_CMAKE_EXE%" (
    echo [ParallelROAM] portable CMake setup finished but cmake.exe was not found.
    goto :fail
)
set "CMAKE_EXE=%PORTABLE_CMAKE_EXE%"

:cmake_ready
echo [ParallelROAM] cmake: %CMAKE_EXE%
"%CMAKE_EXE%" --version >nul 2>nul
if errorlevel 1 (
    echo [ParallelROAM] CMake not found.
    echo [ParallelROAM] Portable CMake setup failed; check network access or install CMake manually.
    goto :fail
)

"%CMAKE_EXE%" --preset "%PRESET%"
if errorlevel 1 goto :fail

echo [ParallelROAM] build: %PRESET%
"%CMAKE_EXE%" --build --preset "%PRESET%" --parallel
if errorlevel 1 goto :fail

set "EXE=%PROJECT_ROOT%\build\%PRESET%\bin\ParallelROAM.exe"
if not exist "%EXE%" (
    echo [ParallelROAM] executable not found: %EXE%
    goto :fail
)

echo [ParallelROAM] run: %EXE% !RUN_ARGS!
"%EXE%" !RUN_ARGS!
if errorlevel 1 goto :fail

popd
exit /b 0

:fail
popd
exit /b 1
