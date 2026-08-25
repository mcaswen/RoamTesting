param(
    [switch]$SkipBuild,
    [switch]$VerifyOnly,
    [switch]$Candidate
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

$repositoryRoot = (Resolve-Path (Join-Path $PSScriptRoot "../../..")).Path
$manifestPath = Join-Path $repositoryRoot "docs/parallel-roam/baselines/cbt-2024-official-v1.json"
$outputRoot = Join-Path $repositoryRoot "benchmark-output/cbt-2024-official-baseline-v1"
$rawRoot = Join-Path $outputRoot "raw"
$reportRoot = Join-Path $repositoryRoot "benchmark-output"
$preset = "relwithdebinfo-d3d12-fetch"
$configuration = "RelWithDebInfo"
$repeatCount = 3
$invariantCulture = [Globalization.CultureInfo]::InvariantCulture
$utf8NoBom = New-Object System.Text.UTF8Encoding($false)

function Write-Utf8NoBom([string]$Path, [string]$Text)
{
    [IO.File]::WriteAllText($Path, $Text, $utf8NoBom)
}

function Read-Manifest
{
    if (-not (Test-Path -LiteralPath $manifestPath -PathType Leaf))
    {
        throw "CBT baseline manifest is missing: $manifestPath"
    }
    return Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
}

function Get-CanonicalSha256([string]$Path, [bool]$Binary)
{
    $bytes = [IO.File]::ReadAllBytes($Path)
    if (-not $Binary)
    {
        # Git may check text out as LF or CRLF. Hash the repository-normalized UTF-8 form.
        $strictUtf8 = New-Object System.Text.UTF8Encoding($false, $true)
        $text = $strictUtf8.GetString($bytes).Replace("`r`n", "`n").Replace("`r", "`n")
        $bytes = $utf8NoBom.GetBytes($text)
    }
    $sha256 = [Security.Cryptography.SHA256]::Create()
    try
    {
        return (($sha256.ComputeHash($bytes) | ForEach-Object { $_.ToString("x2") }) -join "")
    }
    finally
    {
        $sha256.Dispose()
    }
}

function Assert-FrozenInputs($Manifest)
{
    if ($Manifest.baselineId -ne "cbt-2024-official-baseline-v1")
    {
        throw "Unexpected CBT baseline id: $($Manifest.baselineId)"
    }
    if (@($Manifest.frozenFiles).Count -eq 0)
    {
        throw "The CBT baseline manifest does not contain frozen file hashes"
    }

    foreach ($entry in $Manifest.frozenFiles)
    {
        $fullPath = [IO.Path]::GetFullPath((Join-Path $repositoryRoot ([string]$entry.path)))
        if (-not $fullPath.StartsWith($repositoryRoot, [StringComparison]::OrdinalIgnoreCase))
        {
            throw "Frozen path escapes the repository: $($entry.path)"
        }
        if (-not (Test-Path -LiteralPath $fullPath -PathType Leaf))
        {
            throw "Frozen input is missing: $($entry.path)"
        }
        $binary = $entry.PSObject.Properties.Name -contains "binary" -and [bool]$entry.binary
        $actualHash = Get-CanonicalSha256 $fullPath $binary
        if ($actualHash -ne ([string]$entry.sha256).ToLowerInvariant())
        {
            throw "Frozen input changed: $($entry.path)`nexpected $($entry.sha256)`nactual   $actualHash"
        }
    }
}

function Resolve-GitIdentity($Manifest)
{
    $head = (& git -C $repositoryRoot rev-parse HEAD).Trim()
    if ($LASTEXITCODE -ne 0)
    {
        throw "Unable to resolve the repository HEAD"
    }

    if ($Candidate)
    {
        return [PSCustomObject]@{ Head = $head; Tag = "candidate-unpublished" }
    }

    $tag = [string]$Manifest.benchmarkTag
    $tagCommit = (& git -C $repositoryRoot rev-parse --verify "$tag^{commit}" 2>$null)
    if ($LASTEXITCODE -ne 0)
    {
        throw "Required immutable benchmark tag is missing: $tag"
    }
    $tagCommit = $tagCommit.Trim()
    if ($tagCommit -ne $head)
    {
        throw "HEAD $head does not match benchmark tag $tag ($tagCommit)"
    }
    $trackedChanges = (& git -C $repositoryRoot status --porcelain --untracked-files=no)
    if ($LASTEXITCODE -ne 0 -or $trackedChanges)
    {
        throw "A formal baseline run requires a clean tracked worktree"
    }
    return [PSCustomObject]@{ Head = $head; Tag = $tag }
}

function Resolve-CMake
{
    $portableCMake = Join-Path $repositoryRoot "tools/cmake/bin/cmake.exe"
    if (Test-Path -LiteralPath $portableCMake -PathType Leaf)
    {
        return $portableCMake
    }
    return (Get-Command cmake -ErrorAction Stop).Source
}

function Build-Baseline
{
    $cmake = Resolve-CMake
    & $cmake --preset $preset
    if ($LASTEXITCODE -ne 0)
    {
        throw "CMake configure failed for $preset"
    }
    & $cmake --build --preset $preset --parallel
    if ($LASTEXITCODE -ne 0)
    {
        throw "CMake build failed for $preset"
    }
}

function Parse-Number([string]$Value)
{
    return [double]::Parse($Value, [Globalization.NumberStyles]::Float, $invariantCulture)
}

function Average($Values)
{
    $numbers = @($Values | ForEach-Object { [double]$_ })
    if ($numbers.Count -eq 0) { return 0.0 }
    return ($numbers | Measure-Object -Average).Average
}

function Percentile95($Values)
{
    $numbers = @($Values | Sort-Object)
    if ($numbers.Count -eq 0) { return 0.0 }
    $index = [Math]::Max([Math]::Ceiling($numbers.Count * 0.95) - 1, 0)
    return [double]$numbers[$index]
}

function StandardDeviation($Values)
{
    $numbers = @($Values | ForEach-Object { [double]$_ })
    if ($numbers.Count -le 1) { return 0.0 }
    $mean = Average $numbers
    $sum = 0.0
    foreach ($number in $numbers) { $sum += ($number - $mean) * ($number - $mean) }
    return [Math]::Sqrt($sum / ($numbers.Count - 1))
}

function Assert-UniformValue($Rows, [string]$Property, [string]$Expected)
{
    $different = @($Rows | Where-Object { [string]$_.$Property -ne $Expected })
    if ($different.Count -ne 0)
    {
        throw "Baseline output has unexpected $Property; expected $Expected"
    }
}

function Assert-UniformNumber($Rows, [string]$Property, [double]$Expected)
{
    $different = @($Rows | Where-Object {
        [Math]::Abs((Parse-Number ([string]$_.$Property)) - $Expected) -gt 0.0001
    })
    if ($different.Count -ne 0)
    {
        throw "Baseline output has unexpected $Property; expected $Expected"
    }
}

function Invoke-BenchmarkRun(
    [string]$Executable,
    [string]$PathName,
    $PathManifest,
    $Capacity,
    [int]$Repeat,
    [string]$SessionRoot,
    [string]$Head)
{
    $capacityName = [string]$Capacity.name
    $label = "cbt-2024-official-baseline-v1-$PathName-$($capacityName.ToLowerInvariant())-r$('{0:d2}' -f $Repeat)"
    $arguments = @(
        "--runtime-benchmark",
        "--runtime-benchmark-algorithm", "cbt",
        "--runtime-benchmark-path", $PathName,
        "--runtime-benchmark-cbt-capacity", $capacityName,
        "--runtime-benchmark-cbt-validation", "off",
        "--runtime-benchmark-cbt-geometry", "modified",
        "--runtime-benchmark-label", $label
    )

    Write-Host "[CBT baseline] $PathName $capacityName repeat $Repeat/$repeatCount"
    $programOutput = @(& $Executable @arguments 2>&1 | ForEach-Object { $_.ToString() })
    $programOutput | ForEach-Object { Write-Host $_ }
    if ($LASTEXITCODE -ne 0)
    {
        throw "Runtime benchmark failed for $label"
    }

    $csvLine = $programOutput | Where-Object { $_ -match '^Runtime benchmark csv:\s*(.+)$' } | Select-Object -Last 1
    $markdownLine = $programOutput | Where-Object { $_ -match '^Runtime benchmark report:\s*(.+)$' } | Select-Object -Last 1
    if (-not $csvLine -or -not $markdownLine)
    {
        throw "Runtime benchmark did not report its output paths for $label"
    }
    # std::filesystem::path uses quoted output on Windows, so remove the wrapper
    # before resolving the application-reported relative path.
    $csvRelative = [regex]::Match($csvLine, '^Runtime benchmark csv:\s*(.+)$').Groups[1].Value.Trim().Trim('"')
    $markdownRelative = [regex]::Match($markdownLine, '^Runtime benchmark report:\s*(.+)$').Groups[1].Value.Trim().Trim('"')
    $csvSource = [IO.Path]::GetFullPath((Join-Path $repositoryRoot $csvRelative))
    $markdownSource = [IO.Path]::GetFullPath((Join-Path $repositoryRoot $markdownRelative))
    if (-not $csvSource.StartsWith($reportRoot, [StringComparison]::OrdinalIgnoreCase) -or
        -not $markdownSource.StartsWith($reportRoot, [StringComparison]::OrdinalIgnoreCase))
    {
        throw "Runtime benchmark returned a path outside benchmark-output"
    }

    $fileStem = "$PathName-$($capacityName.ToLowerInvariant())-r$('{0:d2}' -f $Repeat)"
    $csvDestination = Join-Path $SessionRoot "$fileStem.csv"
    $markdownDestination = Join-Path $SessionRoot "$fileStem.md"
    Move-Item -LiteralPath $csvSource -Destination $csvDestination
    Move-Item -LiteralPath $markdownSource -Destination $markdownDestination

    $rows = @(Import-Csv -LiteralPath $csvDestination | Where-Object {
        $_.algorithmKey -eq "cbt_2024_official_baseline_v1"
    })
    $expectedSamples = [int]$PathManifest.sampleCount
    if ($rows.Count -ne $expectedSamples)
    {
        throw "$label produced $($rows.Count) samples; expected $expectedSamples"
    }
    for ($index = 0; $index -lt $rows.Count; ++$index)
    {
        if ([int]$rows[$index].pathSampleIndex -ne $index -or [int]$rows[$index].pathSampleCount -ne $expectedSamples)
        {
            throw "$label has an incomplete discrete camera path at sample $index"
        }
    }

    Assert-UniformValue $rows "buildConfiguration" $configuration
    Assert-UniformValue $rows "graphicsBackend" "D3D12"
    Assert-UniformValue $rows "vSyncEnabled" "false"
    Assert-UniformValue $rows "drawableWidth" ([string]$Manifest.renderer.drawableWidth)
    Assert-UniformValue $rows "drawableHeight" ([string]$Manifest.renderer.drawableHeight)
    Assert-UniformValue $rows "heightMapPath" ([string]$PathManifest.heightMap)
    Assert-UniformValue $rows "heightMapWidth" ([string]$PathManifest.heightMapWidth)
    Assert-UniformValue $rows "heightMapHeight" ([string]$PathManifest.heightMapHeight)
    Assert-UniformValue $rows "maxDepthSetting" ([string]$PathManifest.maxDepth)
    Assert-UniformNumber $rows "terrainSize" ([double]$PathManifest.terrainSize)
    Assert-UniformNumber $rows "heightScale" ([double]$PathManifest.heightScale)
    Assert-UniformNumber $rows "cbtTriangleAreaPixels" ([double]$PathManifest.cbtTriangleAreaPixels)
    Assert-UniformValue $rows "cbtCapacity" ([string]$Capacity.elements)
    Assert-UniformValue $rows "cbtValidationMode" "Off"
    Assert-UniformValue $rows "cbtGeometryMode" "ModifiedOnly"
    Assert-UniformValue $rows "cbtDiagnosticSampleDropped" "false"

    $badGeneration = @($rows | Where-Object {
        $_.cbtDiagnosticSampleGeneration -ne $_.cbtGpuTimingSampleGeneration -or
        $_.cbtGpuTimingSampleGeneration -ne $_.cbtTerrainRenderSampleGeneration -or
        ([int64]$_.cbtTopologyGeneration - [int64]$_.cbtDiagnosticSampleGeneration) -ne
            [int64]$_.cbtDiagnosticSampleAge
    })
    if ($badGeneration.Count -ne 0)
    {
        throw "$label contains $($badGeneration.Count) stale or cross-generation samples"
    }
    $maxInvalidTopology = ($rows | ForEach-Object { [int64]$_.invalidTopology } | Measure-Object -Maximum).Maximum
    if ($maxInvalidTopology -ne 0)
    {
        throw "$label reported $maxInvalidTopology topology errors"
    }

    $frameTimes = @($rows | ForEach-Object { Parse-Number $_.frameMilliseconds })
    $lodTimes = @($rows | ForEach-Object { Parse-Number $_.lodTotalMilliseconds })
    $gpuTimes = @($rows | ForEach-Object { Parse-Number $_.cbtGpuStageSumMilliseconds })
    $triangles = @($rows | ForEach-Object { Parse-Number $_.triangles })
    $activeSlots = @($rows | ForEach-Object { Parse-Number $_.cbtActiveDynamicSlots })
    $remainingSlots = @($rows | ForEach-Object { Parse-Number $_.cbtRemainingDynamicSlots })
    $committedSlots = @($rows | ForEach-Object { Parse-Number $_.cbtCommittedSlots })
    $releasedSlots = @($rows | ForEach-Object { Parse-Number $_.cbtReleasedSlots })

    return [PSCustomObject]@{
        baselineId = [string]$Manifest.baselineId
        sourceCommit = $Head
        path = $PathName
        capacity = $capacityName
        capacityElements = [int64]$Capacity.elements
        repeat = $Repeat
        samples = $rows.Count
        averageFrameMilliseconds = Average $frameTimes
        p95FrameMilliseconds = Percentile95 $frameTimes
        averageLodMilliseconds = Average $lodTimes
        p95LodMilliseconds = Percentile95 $lodTimes
        averageGpuStageMilliseconds = Average $gpuTimes
        p95GpuStageMilliseconds = Percentile95 $gpuTimes
        averageTriangles = Average $triangles
        p95Triangles = Percentile95 $triangles
        maxTriangles = ($triangles | Measure-Object -Maximum).Maximum
        averageActiveDynamicSlots = Average $activeSlots
        averageRemainingDynamicSlots = Average $remainingSlots
        minimumRemainingDynamicSlots = ($remainingSlots | Measure-Object -Minimum).Minimum
        averageCommittedSlots = Average $committedSlots
        averageReleasedSlots = Average $releasedSlots
        maxDepthReached = ($rows | ForEach-Object { [int]$_.maxDepthReached } | Measure-Object -Maximum).Maximum
        maxTopologyIssues = $maxInvalidTopology
        rawCsvSha256 = (Get-FileHash -Algorithm SHA256 -LiteralPath $csvDestination).Hash.ToLowerInvariant()
    }
}

function Build-AggregateRows($RunRows)
{
    $aggregateRows = @()
    foreach ($group in ($RunRows | Group-Object path, capacity))
    {
        $first = $group.Group[0]
        $aggregateRows += [PSCustomObject]@{
            baselineId = $first.baselineId
            path = $first.path
            capacity = $first.capacity
            capacityElements = $first.capacityElements
            repeats = $group.Count
            samplesPerRepeat = $first.samples
            meanFrameMilliseconds = Average ($group.Group.averageFrameMilliseconds)
            repeatStdDevFrameMilliseconds = StandardDeviation ($group.Group.averageFrameMilliseconds)
            meanLodMilliseconds = Average ($group.Group.averageLodMilliseconds)
            repeatStdDevLodMilliseconds = StandardDeviation ($group.Group.averageLodMilliseconds)
            meanGpuStageMilliseconds = Average ($group.Group.averageGpuStageMilliseconds)
            repeatStdDevGpuStageMilliseconds = StandardDeviation ($group.Group.averageGpuStageMilliseconds)
            meanTriangles = Average ($group.Group.averageTriangles)
            repeatStdDevTriangles = StandardDeviation ($group.Group.averageTriangles)
            meanActiveDynamicSlots = Average ($group.Group.averageActiveDynamicSlots)
            meanRemainingDynamicSlots = Average ($group.Group.averageRemainingDynamicSlots)
            minimumRemainingDynamicSlots = ($group.Group.minimumRemainingDynamicSlots | Measure-Object -Minimum).Minimum
            meanCommittedSlots = Average ($group.Group.averageCommittedSlots)
            meanReleasedSlots = Average ($group.Group.averageReleasedSlots)
            maxDepthReached = ($group.Group.maxDepthReached | Measure-Object -Maximum).Maximum
            maxTopologyIssues = ($group.Group.maxTopologyIssues | Measure-Object -Maximum).Maximum
        }
    }
    return $aggregateRows | Sort-Object path, capacityElements
}

function Format-Decimal([double]$Value)
{
    return $Value.ToString("F4", $invariantCulture)
}

function Write-Summaries($Manifest, $GitIdentity, $RunRows, $AggregateRows, [string]$SessionName)
{
    New-Item -ItemType Directory -Path $outputRoot -Force | Out-Null
    $runCsv = (($RunRows | ConvertTo-Csv -NoTypeInformation) -join "`r`n") + "`r`n"
    $aggregateCsv = (($AggregateRows | ConvertTo-Csv -NoTypeInformation) -join "`r`n") + "`r`n"
    Write-Utf8NoBom (Join-Path $outputRoot "run-summary.csv") $runCsv
    Write-Utf8NoBom (Join-Path $outputRoot "capacity-summary.csv") $aggregateCsv

    $lines = [Collections.Generic.List[string]]::new()
    $lines.Add("# CBT 2024 official-semantics baseline v1")
    $lines.Add("")
    $lines.Add("- Baseline ID: $($Manifest.baselineId)")
    $lines.Add("- Algorithm key: $($Manifest.algorithmKey)")
    $lines.Add("- Git commit: $($GitIdentity.Head)")
    $lines.Add("- Benchmark tag: $($GitIdentity.Tag)")
    $lines.Add("- Raw report directory: raw/$SessionName (generated locally and excluded from Git)")
    $lines.Add("- Repeats: $repeatCount; validation=Off; geometry=ModifiedOnly; VSync=Off")
    $lines.Add("- License status: upstream has no declared license; public redistribution remains blocked")
    $lines.Add("")
    $lines.Add("| Path | Capacity | Repeats | Samples/run | Mean triangles | Repeat SD | Mean GPU stages ms | Repeat SD | Mean remaining | Min remaining | Max depth | Issues |")
    $lines.Add("| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |")
    foreach ($row in $AggregateRows)
    {
        $lines.Add("| $($row.path) | $($row.capacity) | $($row.repeats) | $($row.samplesPerRepeat) | " +
            "$(Format-Decimal $row.meanTriangles) | $(Format-Decimal $row.repeatStdDevTriangles) | " +
            "$(Format-Decimal $row.meanGpuStageMilliseconds) | $(Format-Decimal $row.repeatStdDevGpuStageMilliseconds) | " +
            "$(Format-Decimal $row.meanRemainingDynamicSlots) | $(Format-Decimal $row.minimumRemainingDynamicSlots) | " +
            "$($row.maxDepthReached) | $($row.maxTopologyIssues) |")
    }
    $lines.Add("")
    $lines.Add("Per-run data: [run-summary.csv](run-summary.csv); aggregate data: [capacity-summary.csv](capacity-summary.csv)")
    Write-Utf8NoBom (Join-Path $outputRoot "README.md") (($lines -join "`r`n") + "`r`n")
}

Push-Location $repositoryRoot
try
{
    $Manifest = Read-Manifest
    Assert-FrozenInputs $Manifest
    $gitIdentity = Resolve-GitIdentity $Manifest
    Write-Host "[CBT baseline] manifest verified for $($gitIdentity.Head)"
    if ($VerifyOnly)
    {
        return
    }
    if (-not $SkipBuild)
    {
        Build-Baseline
    }

    $executable = Join-Path $repositoryRoot "build/$preset/bin/ParallelROAM.exe"
    if (-not (Test-Path -LiteralPath $executable -PathType Leaf))
    {
        throw "D3D12 baseline executable is missing: $executable"
    }
    $sessionName = (Get-Date).ToString("yyyyMMdd-HHmmss", $invariantCulture)
    $sessionRoot = Join-Path $rawRoot $sessionName
    New-Item -ItemType Directory -Path $sessionRoot -Force | Out-Null

    $runRows = @()
    foreach ($pathName in @("default", "extreme"))
    {
        $pathManifest = $Manifest.paths.$pathName
        foreach ($capacity in $Manifest.capacities)
        {
            for ($repeat = 1; $repeat -le $repeatCount; ++$repeat)
            {
                $runRows += Invoke-BenchmarkRun $executable $pathName $pathManifest $capacity $repeat $sessionRoot $gitIdentity.Head
            }
        }
    }
    $aggregateRows = @(Build-AggregateRows $runRows)
    Write-Summaries $Manifest $gitIdentity $runRows $aggregateRows $sessionName
    Write-Host "[CBT baseline] summaries: $outputRoot"
}
finally
{
    Pop-Location
}
