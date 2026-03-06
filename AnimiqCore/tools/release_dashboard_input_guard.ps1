param(
    [string]$ReportDir = ".\build\reports",
    [switch]$IncludeWinUi,
    [switch]$RequireUnityMiqForFull = $true,
    [switch]$RequireOnboardingKpiForFull = $true,
    [string]$OutputJson = ".\build\reports\release_dashboard_input_guard.json",
    [string]$OutputTxt = ".\build\reports\release_dashboard_input_guard.txt"
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
$resolvedReportDir = Resolve-AbsolutePath -Path $ReportDir -BaseDirectory $repoRoot
$resolvedJson = Resolve-AbsolutePath -Path $OutputJson -BaseDirectory $repoRoot
$resolvedTxt = Resolve-AbsolutePath -Path $OutputTxt -BaseDirectory $repoRoot
New-Item -ItemType Directory -Force -Path (Split-Path -Parent $resolvedJson) | Out-Null
New-Item -ItemType Directory -Force -Path (Split-Path -Parent $resolvedTxt) | Out-Null

$required = [System.Collections.Generic.List[string]]::new()
$required.Add((Join-Path $resolvedReportDir "vsfavatar_gate_summary.txt"))
$required.Add((Join-Path $resolvedReportDir "vrm_gate_fixed5.txt"))
$required.Add((Join-Path $resolvedReportDir "vxavatar_gate_summary.txt"))
$required.Add((Join-Path $resolvedReportDir "host_publish_latest.txt"))
$required.Add((Join-Path $resolvedReportDir "host_e2e_gate_summary.txt"))
$required.Add((Join-Path $resolvedReportDir "tracking_parser_fuzz_gate_summary.txt"))
$required.Add((Join-Path $resolvedReportDir "mediapipe_sidecar_sanity_summary.txt"))

if ($IncludeWinUi) {
    $required.Add((Join-Path $resolvedReportDir "winui_xaml_min_repro_summary.txt"))
}
if ($RequireUnityMiqForFull) {
    $required.Add((Join-Path $resolvedReportDir "unity_miq_lts_gate_summary.txt"))
}
if ($RequireOnboardingKpiForFull) {
    $required.Add((Join-Path $resolvedReportDir "onboarding_kpi_summary.json"))
}

$rows = @()
$missing = @()
foreach ($path in $required) {
    $exists = Test-Path -LiteralPath $path
    if (-not $exists) { $missing += $path }
    $rows += [ordered]@{
        path = $path
        exists = [bool]$exists
    }
}

$obj = [ordered]@{
    generated_utc = (Get-Date).ToUniversalTime().ToString("o")
    report_dir = $resolvedReportDir
    include_winui = [bool]$IncludeWinUi
    require_unity_miq_for_full = [bool]$RequireUnityMiqForFull
    require_onboarding_kpi_for_full = [bool]$RequireOnboardingKpiForFull
    required_count = [int]$required.Count
    missing_count = [int]$missing.Count
    pass = ($missing.Count -eq 0)
    rows = $rows
}
$obj | ConvertTo-Json -Depth 5 | Set-Content -Path $resolvedJson -Encoding UTF8

$lines = [System.Collections.Generic.List[string]]::new()
$lines.Add("Release Dashboard Input Guard")
$lines.Add("GeneratedUtc: $($obj.generated_utc)")
$lines.Add("ReportDir: $resolvedReportDir")
$lines.Add("RequiredCount: $($obj.required_count)")
$lines.Add("MissingCount: $($obj.missing_count)")
$lines.Add("Overall: $(if ($obj.pass) { "PASS" } else { "FAIL" })")
foreach ($row in $rows) {
    $lines.Add("- $(if ($row.exists) { 'PASS' } else { 'MISSING' }): $($row.path)")
}
$lines | Set-Content -Path $resolvedTxt -Encoding UTF8

Write-Host "json=$resolvedJson"
Write-Host "txt=$resolvedTxt"

if (-not $obj.pass) { exit 1 }

