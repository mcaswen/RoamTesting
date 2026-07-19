param(
    [double]$MinimumPercent = 15.0
)

$projectRoot = Resolve-Path (Join-Path $PSScriptRoot "..")
$sourcePaths = @(
    "src/algorithms/TerrainLodView.cpp",
    "src/algorithms/TerrainLodView.h",
    "src/algorithms/cbt_2024/Cbt2024Support.cpp",
    "src/algorithms/cbt_2024/Cbt2024Support.h",
    "src/algorithms/cbt_2024/d3d12/D3D12CbtTerrainLodAlgorithm.cpp",
    "src/algorithms/cbt_2024/d3d12/D3D12CbtTerrainLodAlgorithm.h",
    "src/render/D3D12ProceduralTerrainPipeline.cpp",
    "src/render/D3D12ProceduralTerrainPipeline.h"
)

function Measure-CommentCoverage([string]$relativePath)
{
    $fullPath = Join-Path $projectRoot $relativePath
    if (-not (Test-Path -LiteralPath $fullPath -PathType Leaf))
    {
        throw "Comment coverage source is missing: $relativePath"
    }

    $lines = Get-Content -LiteralPath $fullPath -Encoding UTF8
    $nonEmptyLines = @($lines | Where-Object { $_.Trim().Length -gt 0 }).Count
    $commentLines = @($lines | Where-Object { $_ -match '//|/\*|\*/' }).Count
    $rate = if ($nonEmptyLines -gt 0) { 100.0 * $commentLines / $nonEmptyLines } else { 0.0 }
    return [pscustomobject]@{
        Path = $relativePath
        NonEmptyLines = $nonEmptyLines
        CommentLines = $commentLines
        Rate = $rate
    }
}

$failed = $false
$totalNonEmptyLines = 0
$totalCommentLines = 0
foreach ($sourcePath in $sourcePaths)
{
    $coverage = Measure-CommentCoverage $sourcePath
    $totalNonEmptyLines += $coverage.NonEmptyLines
    $totalCommentLines += $coverage.CommentLines
    Write-Host ("{0}: {1}/{2} = {3:N1}%" -f `
        $coverage.Path,
        $coverage.CommentLines,
        $coverage.NonEmptyLines,
        $coverage.Rate)
    if ($coverage.Rate + 0.0001 -lt $MinimumPercent)
    {
        $failed = $true
    }
}

$totalRate = 100.0 * $totalCommentLines / [Math]::Max($totalNonEmptyLines, 1)
Write-Host ("Combined: {0}/{1} = {2:N1}%" -f $totalCommentLines, $totalNonEmptyLines, $totalRate)
if ($totalRate + 0.0001 -lt $MinimumPercent)
{
    $failed = $true
}

if ($failed)
{
    Write-Error "CBT C++ comment coverage is below the required $MinimumPercent%."
    exit 1
}
