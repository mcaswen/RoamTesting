param()

$projectRoot = Resolve-Path (Join-Path $PSScriptRoot "..")
$sourceExtensions = @(".h", ".cpp", ".hlsl", ".hlsli")

function Measure-CommentCoverage([string]$label, [string[]]$relativePaths)
{
    $files = @()
    foreach ($relativePath in $relativePaths)
    {
        $fullPath = Join-Path $projectRoot $relativePath
        if (-not (Test-Path -LiteralPath $fullPath))
        {
            throw "Comment coverage path is missing: $relativePath"
        }

        if (Test-Path -LiteralPath $fullPath -PathType Container)
        {
            $files += Get-ChildItem -LiteralPath $fullPath -Recurse -File |
                Where-Object { $sourceExtensions -contains $_.Extension.ToLowerInvariant() }
        }
        elseif ($sourceExtensions -contains ([System.IO.Path]::GetExtension($fullPath)).ToLowerInvariant())
        {
            $files += Get-Item -LiteralPath $fullPath
        }
    }
    $files = @($files | Sort-Object -Property FullName -Unique)

    $codeLines = 0
    $commentLines = 0
    foreach ($file in $files)
    {
        $lines = Get-Content -LiteralPath $file.FullName -Encoding UTF8
        foreach ($line in $lines)
        {
            $trimmed = $line.Trim()
            if ($trimmed.Length -eq 0)
            {
                continue
            }

            if ($line -match '//|/\*|\*/')
            {
                $commentLines += 1
            }

            # 纯结构和预处理行不承载业务逻辑，否则会鼓励给字段和样板赋值补翻译注释
            if ($trimmed -match '^(//|/\*|\*|\*/)' -or
                $trimmed -match '^#' -or
                $trimmed -match '^[{}]+;?$' -or
                $trimmed -match '^(public|private|protected):$' -or
                $trimmed -match '^namespace(\s|$)')
            {
                continue
            }

            $codeLines += 1
        }
    }

    $rate = if ($codeLines -gt 0) { 100.0 * $commentLines / $codeLines } else { 0.0 }
    return [pscustomobject]@{
        Path = $label
        CodeLines = $codeLines
        CommentLines = $commentLines
        Rate = $rate
    }
}

$coverageRequirements = @(
    [pscustomobject]@{ Label = "project source"; Paths = @("src", "assets/shaders"); MinimumPercent = 15.0 },
    [pscustomobject]@{ Label = "assets/shaders"; Paths = @("assets/shaders"); MinimumPercent = 15.0 },
    [pscustomobject]@{ Label = "src/render"; Paths = @("src/render"); MinimumPercent = 12.0 },
    [pscustomobject]@{ Label = "src/algorithms/data_oriented_roam"; Paths = @("src/algorithms/data_oriented_roam"); MinimumPercent = 20.0 },
    [pscustomobject]@{ Label = "src/algorithms/classic_roam"; Paths = @("src/algorithms/classic_roam"); MinimumPercent = 20.0 },
    [pscustomobject]@{ Label = "src/algorithms/cbt_2024"; Paths = @("src/algorithms/cbt_2024"); MinimumPercent = 16.0 },
    [pscustomobject]@{ Label = "src/platform"; Paths = @("src/platform"); MinimumPercent = 12.0 }
)

$failed = $false
foreach ($requirement in $coverageRequirements)
{
    $coverage = Measure-CommentCoverage $requirement.Label $requirement.Paths
    Write-Host ("{0}: {1}/{2} = {3:N1}% (required {4:N1}%)" -f `
        $coverage.Path,
        $coverage.CommentLines,
        $coverage.CodeLines,
        $coverage.Rate,
        $requirement.MinimumPercent)
    if ($coverage.Rate + 0.0001 -lt $requirement.MinimumPercent)
    {
        $failed = $true
    }
}

if ($failed)
{
    Write-Error "C++/HLSL comment coverage does not meet the project and complex-module requirements."
    exit 1
}
