param(
    [string]$SampleDir = "..\sample",
    [string]$AvatarToolPath = ".\build\Release\avatar_tool.exe",
    [int]$IterationsPerSample = 10,
    [string[]]$IncludePatterns = @("*.vrm", "*.xav2", "*.vsfavatar"),
    [string]$SummaryPath = ".\build\reports\avatar_load_soak_gate_summary.txt",
    [double]$MinSuccessRatio = 1.0
)

$ErrorActionPreference = "Stop"

function Resolve-AbsolutePath {
    param([string]$Path, [string]$BaseDirectory)
    if ([System.IO.Path]::IsPathRooted($Path)) {
        return [System.IO.Path]::GetFullPath($Path)
    }
    return [System.IO.Path]::GetFullPath((Join-Path $BaseDirectory $Path))
}

$repoRoot = Split-Path -Parent $PSScriptRoot
$resolvedSampleDir = Resolve-AbsolutePath -Path $SampleDir -BaseDirectory $repoRoot
$resolvedTool = Resolve-AbsolutePath -Path $AvatarToolPath -BaseDirectory $repoRoot
$resolvedSummary = Resolve-AbsolutePath -Path $SummaryPath -BaseDirectory $repoRoot

if (-not (Test-Path $resolvedTool)) {
    throw "avatar_tool not found: $resolvedTool"
}
if (-not (Test-Path $resolvedSampleDir)) {
    throw "sample dir not found: $resolvedSampleDir"
}
if ($IterationsPerSample -le 0) {
    throw "IterationsPerSample must be > 0"
}

$samples = [System.Collections.Generic.List[System.IO.FileInfo]]::new()
foreach ($pattern in $IncludePatterns) {
    Get-ChildItem -Path $resolvedSampleDir -File -Filter $pattern | ForEach-Object { $samples.Add($_) }
}

if ($samples.Count -eq 0) {
    throw "no samples matched patterns under: $resolvedSampleDir"
}

$rows = [System.Collections.Generic.List[object]]::new()
$totalRuns = 0
$passRuns = 0
$failRuns = 0
$started = Get-Date

foreach ($s in $samples) {
    $samplePass = 0
    $sampleFail = 0
    for ($i = 1; $i -le $IterationsPerSample; $i++) {
        $totalRuns++
        $output = & $resolvedTool $s.FullName 2>&1
        $exitCode = $LASTEXITCODE
        $ok = $exitCode -eq 0 -and (($output -join "`n") -match "Load succeeded")
        if ($ok) {
            $samplePass++
            $passRuns++
        } else {
            $sampleFail++
            $failRuns++
        }
    }

    $rows.Add([PSCustomObject]@{
        sample = $s.Name
        runs = $IterationsPerSample
        pass = $samplePass
        fail = $sampleFail
        success_ratio = [Math]::Round(($samplePass / [double]$IterationsPerSample), 4)
    })
}

$overallRatio = if ($totalRuns -eq 0) { 0.0 } else { [Math]::Round(($passRuns / [double]$totalRuns), 6) }
$overall = $overallRatio -ge $MinSuccessRatio
$duration = [Math]::Round(((Get-Date) - $started).TotalSeconds, 3)

$lines = [System.Collections.Generic.List[string]]::new()
$lines.Add("Avatar Load Soak Gate Summary")
$lines.Add("Generated: $(Get-Date -Format o)")
$lines.Add("SampleDir: $resolvedSampleDir")
$lines.Add("AvatarToolPath: $resolvedTool")
$lines.Add("IterationsPerSample: $IterationsPerSample")
$lines.Add("SampleCount: $($samples.Count)")
$lines.Add("TotalRuns: $totalRuns")
$lines.Add("PassRuns: $passRuns")
$lines.Add("FailRuns: $failRuns")
$lines.Add("OverallSuccessRatio: $overallRatio")
$lines.Add("MinSuccessRatio: $MinSuccessRatio")
$lines.Add("DurationSec: $duration")
$lines.Add("")
$lines.Add("Rows:")
foreach ($r in $rows) {
    $lines.Add("- $($r.sample): runs=$($r.runs), pass=$($r.pass), fail=$($r.fail), success_ratio=$($r.success_ratio)")
}
$lines.Add("")
$lines.Add("Gate Overall")
$lines.Add("- Overall: $(if ($overall) { 'PASS' } else { 'FAIL' })")

New-Item -ItemType Directory -Force -Path (Split-Path -Parent $resolvedSummary) | Out-Null
$lines | Set-Content -Path $resolvedSummary -Encoding UTF8
Write-Host "summary=$resolvedSummary"

if (-not $overall) {
    exit 1
}
