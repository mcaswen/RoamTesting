$ErrorActionPreference = "Stop"

. "$PSScriptRoot/../../common.ps1"

$scriptArguments = @("--smoke-test") + $args
Invoke-ParallelRoamPreset -Preset "debug-d3d12-fetch" -Arguments $scriptArguments
