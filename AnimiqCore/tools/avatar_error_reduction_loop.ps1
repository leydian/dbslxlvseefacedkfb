param(
    [Parameter(Mandatory = $true)][string]$ManifestPath,
    [string]$AvatarToolPath = ".\build\Release\avatar_tool.exe",
    [string]$VSeeFaceObservationPath = "",
    [int]$Rounds = 5,
    [switch]$StopWhenP0Zero,
    [switch]$StopWhenConsecutiveP0Zero,
    [int]$ConsecutiveP0ZeroTarget = 3,
    [int]$WarningDebtThreshold = 5,
    [string[]]$SupportedExtensions = @("vrm", "miq"),
    [int]$MinSamples = 30,
    [string]$OutputDir = ".\build\reports\avatar_error_reduction"
)

$ErrorActionPreference = "Stop"

function Resolve-AbsolutePath {
    param([string]$Path)
    if ([System.IO.Path]::IsPathRooted($Path)) {
        return [System.IO.Path]::GetFullPath($Path)
    }
    return [System.IO.Path]::GetFullPath($Path)
}

function Normalize-Extensions {
    param([string[]]$Extensions)
    $set = [System.Collections.Generic.HashSet[string]]::new([System.StringComparer]::OrdinalIgnoreCase)
    foreach ($raw in $Extensions) {
        if ($null -eq $raw) {
            continue
        }
        foreach ($part in ("$raw" -split ",")) {
            $e = $part.Trim().TrimStart(".").ToLowerInvariant()
            if (-not [string]::IsNullOrWhiteSpace($e)) {
                $set.Add($e) | Out-Null
            }
        }
    }
    return @($set)
}

$repoRoot = Split-Path -Parent $PSScriptRoot
$SupportedExtensions = Normalize-Extensions -Extensions $SupportedExtensions
if ($SupportedExtensions.Count -eq 0) {
    throw "SupportedExtensions must not be empty"
}

$manifestAbs = Resolve-AbsolutePath -Path $ManifestPath
$avatarToolAbs = Resolve-AbsolutePath -Path $AvatarToolPath
$obsAbs = if ([string]::IsNullOrWhiteSpace($VSeeFaceObservationPath)) { "" } else { (Resolve-AbsolutePath -Path $VSeeFaceObservationPath) }
$outputDirAbs = Resolve-AbsolutePath -Path $OutputDir
if (-not (Test-Path $outputDirAbs)) {
    New-Item -ItemType Directory -Path $outputDirAbs | Out-Null
}

$validateScript = Join-Path $PSScriptRoot "avatar_benchmark_corpus_validate.ps1"
$benchmarkScript = Join-Path $PSScriptRoot "avatar_engine_differential_benchmark.ps1"
if (-not (Test-Path $validateScript)) {
    throw "missing validate script: $validateScript"
}
if (-not (Test-Path $benchmarkScript)) {
    throw "missing benchmark script: $benchmarkScript"
}

& $validateScript `
    -ManifestPath $manifestAbs `
    -SupportedExtensions $SupportedExtensions `
    -MinSamples $MinSamples `
    -OutputJson (Join-Path $outputDirAbs "corpus_validation.json") `
    -OutputTxt (Join-Path $outputDirAbs "corpus_validation.txt")

$roundRows = [System.Collections.Generic.List[object]]::new()
$consecutiveP0Zero = 0
for ($round = 1; $round -le $Rounds; $round++) {
    $tag = "round$($round.ToString('00'))"
    $summaryJson = Join-Path $outputDirAbs "summary_$tag.json"
    $summaryTxt = Join-Path $outputDirAbs "summary_$tag.txt"
    $taxonomyJson = Join-Path $outputDirAbs "taxonomy_$tag.json"
    $dashboardMd = Join-Path $outputDirAbs "dashboard_$tag.md"

    $invoke = @{
        ManifestPath = $manifestAbs
        AvatarToolPath = $avatarToolAbs
        OutputJson = $summaryJson
        OutputTxt = $summaryTxt
        ErrorTaxonomyJson = $taxonomyJson
        ParityDashboardMd = $dashboardMd
        WarningDebtThreshold = $WarningDebtThreshold
        SupportedExtensions = $SupportedExtensions
    }
    if (-not [string]::IsNullOrWhiteSpace($obsAbs)) {
        $invoke["VSeeFaceObservationPath"] = $obsAbs
    }

    & $benchmarkScript @invoke

    $summary = Get-Content -Path $summaryJson -Raw | ConvertFrom-Json
    $roundRows.Add([PSCustomObject]@{
        round = $round
        generated_utc = "$($summary.generated_utc)"
        total = [int]$summary.total
        p0 = [int]$summary.priority.p0
        p1 = [int]$summary.priority.p1
        p2 = [int]$summary.priority.p2
        pass = [int]$summary.priority.pass
        excluded_rows = [int]$summary.excluded_rows
        summary_json = $summaryJson
        taxonomy_json = $taxonomyJson
        dashboard_md = $dashboardMd
        consecutive_p0_zero = 0
    })

    if ([int]$summary.priority.p0 -eq 0) {
        $consecutiveP0Zero++
    } else {
        $consecutiveP0Zero = 0
    }
    $roundRows[$roundRows.Count - 1].consecutive_p0_zero = $consecutiveP0Zero

    if ($StopWhenConsecutiveP0Zero.IsPresent -and $consecutiveP0Zero -ge $ConsecutiveP0ZeroTarget) {
        break
    }
    if ($StopWhenP0Zero.IsPresent -and [int]$summary.priority.p0 -eq 0) {
        break
    }
}

$trend = [PSCustomObject]@{
    generated_utc = (Get-Date).ToUniversalTime().ToString("o")
    manifest_path = $manifestAbs
    avatar_tool_path = $avatarToolAbs
    vseeface_observation_path = $obsAbs
    rounds_requested = $Rounds
    rounds_executed = $roundRows.Count
    stop_when_p0_zero = $StopWhenP0Zero.IsPresent
    stop_when_consecutive_p0_zero = $StopWhenConsecutiveP0Zero.IsPresent
    consecutive_p0_zero_target = $ConsecutiveP0ZeroTarget
    final_consecutive_p0_zero = $consecutiveP0Zero
    supported_extensions = @($SupportedExtensions)
    min_samples = $MinSamples
    rows = $roundRows
}

$trendJson = Join-Path $outputDirAbs "trend_summary.json"
$trendTxt = Join-Path $outputDirAbs "trend_summary.txt"
$trend | ConvertTo-Json -Depth 8 | Set-Content -Path $trendJson -Encoding UTF8

$lines = @()
$lines += "Avatar Error Reduction Trend"
$lines += "GeneratedUTC: $($trend.generated_utc)"
$lines += "ManifestPath: $($trend.manifest_path)"
$lines += "AvatarToolPath: $($trend.avatar_tool_path)"
$lines += "VSeeFaceObservationPath: $($trend.vseeface_observation_path)"
$lines += "SupportedExtensions: $($trend.supported_extensions -join ',')"
$lines += "MinSamples: $($trend.min_samples)"
$lines += "RoundsRequested/Executed: $($trend.rounds_requested)/$($trend.rounds_executed)"
$lines += "StopWhenP0Zero: $($trend.stop_when_p0_zero)"
$lines += "StopWhenConsecutiveP0Zero: $($trend.stop_when_consecutive_p0_zero)"
$lines += "ConsecutiveP0ZeroTarget/Final: $($trend.consecutive_p0_zero_target)/$($trend.final_consecutive_p0_zero)"
$lines += ""
$lines += "Rounds"
foreach ($r in $roundRows) {
    $lines += "- round=$($r.round) total=$($r.total) p0=$($r.p0) p1=$($r.p1) p2=$($r.p2) pass=$($r.pass) excluded=$($r.excluded_rows) consecutiveP0Zero=$($r.consecutive_p0_zero)"
}
$lines | Set-Content -Path $trendTxt -Encoding UTF8

Write-Host "trend_json=$trendJson"
Write-Host "trend_txt=$trendTxt"
