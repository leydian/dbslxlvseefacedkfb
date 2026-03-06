param(
    [string]$UnityLine = "2021-lts",
    [string]$MatrixPath = ".\tools\unity_lts_matrix.json",
    [string]$UnityEditorPath = "",
    [string]$UnityProjectPath = $env:UNITY_MIQ_PROJECT_PATH,
    [string]$ExpectedUnityVersion = "",
    [string]$ReportDir = ".\build\reports",
    [string]$ReportSuffix = "",
    [double]$MinSizeReductionP50 = 20.0,
    [double]$MinSizeReductionP90 = 10.0,
    [double]$MaxLoadDeltaPct = 5.0,
    [int]$Iterations = 10
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

function Get-MatrixEntry {
    param([string]$RepoRoot, [string]$Path, [string]$Line)
    $resolved = Resolve-AbsolutePath -Path $Path -BaseDirectory $RepoRoot
    if (-not (Test-Path -LiteralPath $resolved)) {
        throw "Unity LTS matrix file not found: $resolved"
    }
    $matrix = Get-Content -Raw -Path $resolved | ConvertFrom-Json
    $entry = $matrix.PSObject.Properties[$Line].Value
    if ($null -eq $entry) {
        throw "Unity line not found in matrix: $Line"
    }
    return $entry
}

$repoRoot = Split-Path -Parent $PSScriptRoot
$entry = Get-MatrixEntry -RepoRoot $repoRoot -Path $MatrixPath -Line $UnityLine
if ([string]::IsNullOrWhiteSpace($ExpectedUnityVersion)) {
    $ExpectedUnityVersion = [string]$entry.expected_unity_version
}
$editorEnvVar = [string]$entry.editor_env_var
if ([string]::IsNullOrWhiteSpace($UnityEditorPath) -and -not [string]::IsNullOrWhiteSpace($editorEnvVar)) {
    $UnityEditorPath = [string][System.Environment]::GetEnvironmentVariable($editorEnvVar)
}

if ([string]::IsNullOrWhiteSpace($UnityEditorPath)) {
    throw "Unity editor path is required. Set -UnityEditorPath or env:$editorEnvVar."
}
if (-not (Test-Path -LiteralPath $UnityEditorPath)) {
    throw "Unity editor executable not found: $UnityEditorPath"
}
if ([string]::IsNullOrWhiteSpace($UnityProjectPath)) {
    throw "Unity project path is required. Set -UnityProjectPath or UNITY_MIQ_PROJECT_PATH."
}
if (-not (Test-Path -LiteralPath $UnityProjectPath)) {
    throw "Unity project path not found: $UnityProjectPath"
}

if ([string]::IsNullOrWhiteSpace($ReportSuffix)) {
    $ReportSuffix = $UnityLine
}

New-Item -ItemType Directory -Force -Path $ReportDir | Out-Null
$unityLogPath = Join-Path $ReportDir "unity_miq_${ReportSuffix}_compression_gate.log"
$unityReportPath = Join-Path $ReportDir "unity_miq_${ReportSuffix}_compression_probe.json"
$outputDir = Join-Path $ReportDir "miq_quality_samples_${ReportSuffix}"
$summaryTxtPath = Join-Path $ReportDir "miq_compression_quality_gate_${ReportSuffix}_summary.txt"
$summaryJsonPath = Join-Path $ReportDir "miq_compression_quality_gate_${ReportSuffix}_summary.json"

$args = @(
    "-batchmode",
    "-nographics",
    "-quit",
    "-projectPath", $UnityProjectPath,
    "-executeMethod", "Animiq.Miq.Editor.MiqCiQuality.RunCompressionGate",
    "-miqQualityReportPath", $unityReportPath,
    "-miqQualityOutputDir", $outputDir,
    "-miqCompressionIterations", "$Iterations",
    "-miqExpectedUnityVersion", $ExpectedUnityVersion,
    "-logFile", $unityLogPath
)

& $UnityEditorPath @args
$unityExitCode = $LASTEXITCODE
if (-not (Test-Path -LiteralPath $unityReportPath)) {
    throw "Unity compression probe report not found: $unityReportPath"
}

$probe = Get-Content -Raw -Path $unityReportPath | ConvertFrom-Json
$gateC1 = ($unityExitCode -eq 0) -and ([string]$probe.overall_status -eq "PASS")
$gateC2 = ([double]$probe.size_reduction_pct -ge $MinSizeReductionP50) -and ([double]$probe.size_reduction_pct -ge $MinSizeReductionP90)
$gateC3 = ([double]$probe.load_delta_pct -le $MaxLoadDeltaPct)
$gateC4 = ([int]$probe.decode_failures -eq 0)
$overall = $gateC1 -and $gateC2 -and $gateC3 -and $gateC4

$summary = [ordered]@{
    unity_line = $UnityLine
    generated = (Get-Date).ToString("s")
    unity_version = [string]$probe.unity_version
    unity_exit_code = $unityExitCode
    thresholds = [ordered]@{
        min_size_reduction_p50 = $MinSizeReductionP50
        min_size_reduction_p90 = $MinSizeReductionP90
        max_load_delta_pct = $MaxLoadDeltaPct
    }
    metrics = [ordered]@{
        uncompressed_bytes = [long]$probe.uncompressed_bytes
        compressed_bytes = [long]$probe.compressed_bytes
        size_reduction_pct = [double]$probe.size_reduction_pct
        iterations = [int]$probe.iterations
        uncompressed_load_ms_avg = [double]$probe.uncompressed_load_ms_avg
        compressed_load_ms_avg = [double]$probe.compressed_load_ms_avg
        load_delta_pct = [double]$probe.load_delta_pct
        decode_failures = [int]$probe.decode_failures
    }
    gates = [ordered]@{
        gate_c1_unity_probe_status = if ($gateC1) { "PASS" } else { "FAIL" }
        gate_c2_size_reduction = if ($gateC2) { "PASS" } else { "FAIL" }
        gate_c3_load_delta = if ($gateC3) { "PASS" } else { "FAIL" }
        gate_c4_decode_failure_rate = if ($gateC4) { "PASS" } else { "FAIL" }
        overall = if ($overall) { "PASS" } else { "FAIL" }
    }
    source_report = $unityReportPath
    unity_log = $unityLogPath
}

$summary | ConvertTo-Json -Depth 8 | Set-Content -Path $summaryJsonPath -Encoding UTF8

$lines = @()
$lines += "MIQ Compression Quality Gate Summary"
$lines += "Generated: $($summary.generated)"
$lines += "UnityLine: $UnityLine"
$lines += "UnityVersion: $($summary.unity_version)"
$lines += "UnityExitCode: $unityExitCode"
$lines += ""
$lines += "Gate Results"
$lines += "- GateC1 (unity probe pass): $($summary.gates.gate_c1_unity_probe_status)"
$lines += "- GateC2 (size reduction threshold): $($summary.gates.gate_c2_size_reduction)"
$lines += "- GateC3 (load delta threshold): $($summary.gates.gate_c3_load_delta)"
$lines += "- GateC4 (decode failure rate zero): $($summary.gates.gate_c4_decode_failure_rate)"
$lines += "- Overall: $($summary.gates.overall)"
$lines += ""
$lines += "Metrics"
$lines += "- uncompressed_bytes=$($summary.metrics.uncompressed_bytes)"
$lines += "- compressed_bytes=$($summary.metrics.compressed_bytes)"
$lines += "- size_reduction_pct=$([string]::Format('{0:0.###}', $summary.metrics.size_reduction_pct))"
$lines += "- uncompressed_load_ms_avg=$([string]::Format('{0:0.###}', $summary.metrics.uncompressed_load_ms_avg))"
$lines += "- compressed_load_ms_avg=$([string]::Format('{0:0.###}', $summary.metrics.compressed_load_ms_avg))"
$lines += "- load_delta_pct=$([string]::Format('{0:0.###}', $summary.metrics.load_delta_pct))"
$lines += "- decode_failures=$($summary.metrics.decode_failures)"
$lines += ""
$lines += "Artifacts"
$lines += "- unity_report=$unityReportPath"
$lines += "- unity_log=$unityLogPath"
$lines += "- summary_json=$summaryJsonPath"
$lines | Set-Content -Path $summaryTxtPath -Encoding UTF8

if ($UnityLine -eq "2021-lts") {
    Copy-Item -Force -Path $unityLogPath -Destination (Join-Path $ReportDir "unity_miq_compression_gate.log")
    Copy-Item -Force -Path $unityReportPath -Destination (Join-Path $ReportDir "unity_miq_compression_probe.json")
    Copy-Item -Force -Path $summaryJsonPath -Destination (Join-Path $ReportDir "miq_compression_quality_gate_summary.json")
    Copy-Item -Force -Path $summaryTxtPath -Destination (Join-Path $ReportDir "miq_compression_quality_gate_summary.txt")
}

if (-not $overall) {
    exit 1
}
exit 0
