$ErrorActionPreference = "Stop"

function Invoke-ParallelRoamPreset {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Preset,

        [string[]]$Arguments = @()
    )

    $projectRoot = Resolve-Path (Join-Path $PSScriptRoot "..")

    Push-Location $projectRoot
    try {
        Write-Host "[ParallelROAM] configure: $Preset"
        cmake --preset $Preset

        Write-Host "[ParallelROAM] build: $Preset"
        cmake --build --preset $Preset --parallel

        $windowsExecutable = Join-Path $projectRoot "build/$Preset/bin/ParallelROAM.exe"
        $unixExecutable = Join-Path $projectRoot "build/$Preset/bin/ParallelROAM"

        if (Test-Path $windowsExecutable) {
            $executable = $windowsExecutable
        }
        else {
            $executable = $unixExecutable
        }

        if (-not (Test-Path $executable)) {
            throw "Executable not found: $executable"
        }

        Write-Host "[ParallelROAM] run: $executable $Arguments"
        & $executable @Arguments
    }
    finally {
        Pop-Location
    }
}
