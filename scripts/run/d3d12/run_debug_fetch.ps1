$ErrorActionPreference = "Stop"

. "$PSScriptRoot/../../common.ps1"

Invoke-ParallelRoamPreset -Preset "debug-d3d12-fetch" -Arguments $args
