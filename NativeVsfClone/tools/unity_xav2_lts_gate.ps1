param(
    [string]$MatrixPath = ".\tools\unity_lts_matrix.json",
    [string]$UnityProjectPath = $env:UNITY_XAV2_PROJECT_PATH,
    [string]$AvatarToolPath = ".\build\Release\avatar_tool.exe",
    [string]$ReportDir = ".\build\reports",
    [string]$OfficialLinesCsv = "2021-lts,2022-lts",
    [string]$CandidateLinesCsv = "2023-lts",
    [switch]$IncludeCandidates
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

function Resolve-AbsolutePath {
    param([string]$Path, [string]$BaseDirectory)
    if ([System.IO.Path]::IsPathRooted($Path)) {
        return [System.IO.Path]::GetFullPath($Path)
    }
    return [System.IO.Path]::GetFullPath((Join-Path $BaseDirectory $Path))
}

function Get-Lines {
    param([string]$Csv)
    return @($Csv.Split(",") | ForEach-Object { $_.Trim() } | Where-Object { -not [string]::IsNullOrWhiteSpace($_) })
}

function Invoke-Gate {
    param(
        [string]$Name,
        [string]$Command,
        [string]$SummaryPath
    )
    Write-Host "[unity_xav2_lts_gate] START: $Name"
    Invoke-Expression $Command
    $exitCode = $LASTEXITCODE
    return [PSCustomObject]@{
        name = $Name
        command = $Command
        exit_code = $exitCode
        status = if ($exitCode -eq 0) { "PASS" } else { "FAIL" }
        summary = $SummaryPath
    }
}

$repoRoot = Split-Path -Parent $PSScriptRoot
$resolvedMatrixPath = Resolve-AbsolutePath -Path $MatrixPath -BaseDirectory $repoRoot
if (-not (Test-Path -LiteralPath $resolvedMatrixPath)) {
    throw "Unity LTS matrix file not found: $resolvedMatrixPath"
}
if ([string]::IsNullOrWhiteSpace($UnityProjectPath)) {
    throw "Unity project path is required. Set -UnityProjectPath or UNITY_XAV2_PROJECT_PATH."
}
if (-not (Test-Path -LiteralPath $UnityProjectPath)) {
    throw "Unity project path not found: $UnityProjectPath"
}
if (-not (Test-Path -LiteralPath $AvatarToolPath)) {
    throw "avatar_tool not found: $AvatarToolPath"
}

New-Item -ItemType Directory -Force -Path $ReportDir | Out-Null

$officialLines = Get-Lines -Csv $OfficialLinesCsv
$candidateLines = Get-Lines -Csv $CandidateLinesCsv
$targetLines = @($officialLines)
if ($IncludeCandidates) {
    $targetLines += $candidateLines
}

if ($targetLines.Count -eq 0) {
    throw "No Unity lines selected."
}

$results = [System.Collections.Generic.List[object]]::new()
foreach ($line in $targetLines) {
    $validateSummary = Join-Path $ReportDir "unity_xav2_${line}_validation_summary.txt"
    $paritySummary = Join-Path $ReportDir "xav2_parity_gate_${line}_summary.txt"
    $compressionSummary = Join-Path $ReportDir "xav2_compression_quality_gate_${line}_summary.txt"

    $validateCmd = "powershell -ExecutionPolicy Bypass -File .\tools\unity_xav2_validate.ps1 -UnityLine $line -MatrixPath `"$resolvedMatrixPath`" -UnityProjectPath `"$UnityProjectPath`" -ReportDir `"$ReportDir`" -ReportSuffix $line"
    $parityCmd = "powershell -ExecutionPolicy Bypass -File .\tools\xav2_parity_gate.ps1 -UnityLine $line -MatrixPath `"$resolvedMatrixPath`" -UnityProjectPath `"$UnityProjectPath`" -AvatarToolPath `"$AvatarToolPath`" -ReportDir `"$ReportDir`" -ReportSuffix $line"
    $compressionCmd = "powershell -ExecutionPolicy Bypass -File .\tools\xav2_compression_quality_gate.ps1 -UnityLine $line -MatrixPath `"$resolvedMatrixPath`" -UnityProjectPath `"$UnityProjectPath`" -ReportDir `"$ReportDir`" -ReportSuffix $line"

    $results.Add((Invoke-Gate -Name "validate:$line" -Command $validateCmd -SummaryPath $validateSummary))
    $results.Add((Invoke-Gate -Name "parity:$line" -Command $parityCmd -SummaryPath $paritySummary))
    $results.Add((Invoke-Gate -Name "compression:$line" -Command $compressionCmd -SummaryPath $compressionSummary))
}

$officialPattern = if ($officialLines.Count -gt 0) {
    ":(" + (($officialLines | ForEach-Object { [Regex]::Escape($_) }) -join "|") + ")$"
} else {
    "$^"
}
$officialFailures = $results | Where-Object {
    $_.status -eq "FAIL" -and ($_.name -match $officialPattern)
}
$overallOfficialPass = ($officialFailures.Count -eq 0)

$summary = [ordered]@{
    generated = (Get-Date).ToString("s")
    official_lines = $officialLines
    candidate_lines = $candidateLines
    include_candidates = [bool]$IncludeCandidates
    gate_policy = "official-lines-all-pass-required"
    overall = if ($overallOfficialPass) { "PASS" } else { "FAIL" }
    results = $results
}

$summaryJsonPath = Join-Path $ReportDir "unity_xav2_lts_gate_summary.json"
$summaryTxtPath = Join-Path $ReportDir "unity_xav2_lts_gate_summary.txt"
$summary | ConvertTo-Json -Depth 8 | Set-Content -Path $summaryJsonPath -Encoding UTF8

$lines = [System.Collections.Generic.List[string]]::new()
$lines.Add("Unity XAV2 LTS Gate Summary")
$lines.Add("Generated: $($summary.generated)")
$lines.Add("OfficialLines: $($officialLines -join ', ')")
$lines.Add("CandidateLines: $($candidateLines -join ', ')")
$lines.Add("IncludeCandidates: $([bool]$IncludeCandidates)")
$lines.Add("GatePolicy: official-lines-all-pass-required")
$lines.Add("Overall: $($summary.overall)")
$lines.Add("")
$lines.Add("Results")
foreach ($row in $results) {
    $lines.Add("- $($row.name): $($row.status) (exit=$($row.exit_code))")
}
$lines.Add("")
$lines.Add("Artifacts")
$lines.Add("- summary_json=$summaryJsonPath")
$lines.Add("- summary_txt=$summaryTxtPath")
$lines | Set-Content -Path $summaryTxtPath -Encoding UTF8

if (-not $overallOfficialPass) {
    exit 1
}
exit 0
