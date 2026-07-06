@echo off
setlocal EnableExtensions

set "CMAKE_VERSION=%~1"
if "%CMAKE_VERSION%"=="" set "CMAKE_VERSION=3.30.5"

set "SCRIPT_DIR=%~dp0"
for %%I in ("%SCRIPT_DIR%..") do set "PROJECT_ROOT=%%~fI"
set "TOOLS_DIR=%PROJECT_ROOT%\tools"
set "CMAKE_DIR=%TOOLS_DIR%\cmake"
set "DOWNLOAD_DIR=%TOOLS_DIR%\downloads"
set "ARCHIVE=%DOWNLOAD_DIR%\cmake-%CMAKE_VERSION%-windows-x86_64.zip"
set "URL=https://github.com/Kitware/CMake/releases/download/v%CMAKE_VERSION%/cmake-%CMAKE_VERSION%-windows-x86_64.zip"

if exist "%CMAKE_DIR%\bin\cmake.exe" (
    echo [ParallelROAM] portable CMake already exists: %CMAKE_DIR%\bin\cmake.exe
    "%CMAKE_DIR%\bin\cmake.exe" --version
    exit /b %errorlevel%
)

echo [ParallelROAM] download CMake %CMAKE_VERSION%
echo [ParallelROAM] url: %URL%

powershell -NoProfile -ExecutionPolicy Bypass -Command ^
    "$ErrorActionPreference = 'Stop';" ^
    "$toolsDir = '%TOOLS_DIR%';" ^
    "$downloadDir = '%DOWNLOAD_DIR%';" ^
    "$cmakeDir = '%CMAKE_DIR%';" ^
    "$archive = '%ARCHIVE%';" ^
    "$url = '%URL%';" ^
    "$extractDir = Join-Path $downloadDir 'cmake-%CMAKE_VERSION%-extract';" ^
    "New-Item -ItemType Directory -Force -Path $downloadDir | Out-Null;" ^
    "Invoke-WebRequest -Uri $url -OutFile $archive;" ^
    "if (Test-Path $extractDir) { Remove-Item -Recurse -Force $extractDir };" ^
    "Expand-Archive -Path $archive -DestinationPath $extractDir -Force;" ^
    "$root = Get-ChildItem -Path $extractDir -Directory | Select-Object -First 1;" ^
    "if ($null -eq $root) { throw 'CMake archive layout is unexpected' };" ^
    "if (Test-Path $cmakeDir) { Remove-Item -Recurse -Force $cmakeDir };" ^
    "New-Item -ItemType Directory -Force -Path $cmakeDir | Out-Null;" ^
    "Copy-Item -Path (Join-Path $root.FullName '*') -Destination $cmakeDir -Recurse -Force;" ^
    "Remove-Item -Recurse -Force $extractDir;" ^
    "Write-Host '[ParallelROAM] portable CMake ready:' (Join-Path $cmakeDir 'bin\cmake.exe')"

if errorlevel 1 exit /b 1

"%CMAKE_DIR%\bin\cmake.exe" --version
exit /b %errorlevel%
