$ErrorActionPreference = "Stop"

. "$PSScriptRoot/../../common.ps1"

Invoke-ParallelRoamPreset -Preset "release-d3d12-fetch" -Arguments $args
