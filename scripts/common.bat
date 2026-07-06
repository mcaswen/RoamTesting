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

pushd "%PROJECT_ROOT%" || exit /b 1

echo [ParallelROAM] configure: %PRESET%
cmake --preset "%PRESET%"
if errorlevel 1 goto :fail

echo [ParallelROAM] build: %PRESET%
cmake --build --preset "%PRESET%" --parallel
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
