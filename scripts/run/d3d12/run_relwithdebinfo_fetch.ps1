$ErrorActionPreference = "Stop"

. "$PSScriptRoot/../../common.ps1"

Invoke-ParallelRoamPreset -Preset "relwithdebinfo-d3d12-fetch" -Arguments $args
