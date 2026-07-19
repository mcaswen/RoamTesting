param()

$projectRoot = Resolve-Path (Join-Path $PSScriptRoot "..")

function Measure-CommentCoverage([string]$relativePath)
{
    $fullPath = Join-Path $projectRoot $relativePath
    if (-not (Test-Path -LiteralPath $fullPath))
    {
        throw "Comment coverage path is missing: $relativePath"
    }

    $files = if (Test-Path -LiteralPath $fullPath -PathType Container)
    {
        Get-ChildItem -LiteralPath $fullPath -Recurse -File -Include *.h,*.cpp
    }
    else
    {
        Get-Item -LiteralPath $fullPath
    }

    $nonEmptyLines = 0
    $commentLines = 0
    foreach ($file in $files)
    {
        $lines = Get-Content -LiteralPath $file.FullName -Encoding UTF8
        $nonEmptyLines += @($lines | Where-Object { $_.Trim().Length -gt 0 }).Count
        $commentLines += @($lines | Where-Object { $_ -match '//|/\*|\*/' }).Count
    }

    $rate = if ($nonEmptyLines -gt 0) { 100.0 * $commentLines / $nonEmptyLines } else { 0.0 }
    return [pscustomobject]@{
        Path = $relativePath
        NonEmptyLines = $nonEmptyLines
        CommentLines = $commentLines
        Rate = $rate
    }
}

$coverageRequirements = @(
    [pscustomobject]@{ Path = "src"; MinimumPercent = 15.0 },
    [pscustomobject]@{ Path = "src/algorithms/gpu_roam"; MinimumPercent = 16.0 },
    [pscustomobject]@{ Path = "src/render"; MinimumPercent = 16.0 },
    [pscustomobject]@{ Path = "src/algorithms/data_oriented_roam"; MinimumPercent = 16.0 },
    [pscustomobject]@{ Path = "src/algorithms/classic_roam"; MinimumPercent = 16.0 },
    [pscustomobject]@{ Path = "src/algorithms/cbt_2024"; MinimumPercent = 16.0 },
    [pscustomobject]@{ Path = "src/platform"; MinimumPercent = 12.0 }
)

$failed = $false
foreach ($requirement in $coverageRequirements)
{
    $coverage = Measure-CommentCoverage $requirement.Path
    Write-Host ("{0}: {1}/{2} = {3:N1}% (required {4:N1}%)" -f `
        $coverage.Path,
        $coverage.CommentLines,
        $coverage.NonEmptyLines,
        $coverage.Rate,
        $requirement.MinimumPercent)
    if ($coverage.Rate + 0.0001 -lt $requirement.MinimumPercent)
    {
        $failed = $true
    }
}

if ($failed)
{
    Write-Error "C++ comment coverage does not meet the project and complex-module requirements."
    exit 1
}
