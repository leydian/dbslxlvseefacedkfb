param(
    [string]$Configuration = "Release",
    [switch]$NoRestore,
    [switch]$RunPublishHosts,
    [string]$SummaryPath = ".\build\reports\winui_blocker_triage_summary.txt",
    [string]$SummaryJsonPath = ".\build\reports\winui_blocker_triage_summary.json"
)

$ErrorActionPreference = "Stop"

function Resolve-AbsolutePath {
    param([string]$Path, [string]$BaseDirectory)
    if ([System.IO.Path]::IsPathRooted($Path)) {
        return [System.IO.Path]::GetFullPath($Path)
    }
    return [System.IO.Path]::GetFullPath((Join-Path $BaseDirectory $Path))
}

function Get-KeyValueFromSummary {
    param([string]$Path, [string]$Key)
    if (-not (Test-Path -LiteralPath $Path)) { return "" }
    $line = Select-String -Path $Path -Pattern ("^" + [Regex]::Escape($Key) + ":\s*(.+)$") | Select-Object -First 1
    if ($null -eq $line) { return "" }
    return $line.Matches[0].Groups[1].Value.Trim()
}

$repoRoot = Split-Path -Parent $PSScriptRoot
$resolvedSummary = Resolve-AbsolutePath -Path $SummaryPath -BaseDirectory $repoRoot
$resolvedSummaryJson = Resolve-AbsolutePath -Path $SummaryJsonPath -BaseDirectory $repoRoot
$reportDir = Join-Path $repoRoot "build\reports"
New-Item -ItemType Directory -Force -Path (Split-Path -Parent $resolvedSummary) | Out-Null
New-Item -ItemType Directory -Force -Path (Split-Path -Parent $resolvedSummaryJson) | Out-Null
New-Item -ItemType Directory -Force -Path (Join-Path $reportDir "winui") | Out-Null

$steps = [System.Collections.Generic.List[object]]::new()

Push-Location $repoRoot
try {
    if ($RunPublishHosts) {
        & powershell -ExecutionPolicy Bypass -File .\tools\publish_hosts.ps1 -IncludeWinUi -SkipNativeBuild @(
            if ($NoRestore) { "-NoRestore" }
        )
        $steps.Add([ordered]@{
            name = "publish_hosts_include_winui"
            exit_code = $LASTEXITCODE
        })
    }

    $minReproSummaryPath = Join-Path $reportDir "winui_xaml_min_repro_summary.txt"
    $minReproSummaryJsonPath = Join-Path $reportDir "winui_xaml_min_repro_summary.json"
    $minReproArgs = @(
        "-ExecutionPolicy", "Bypass",
        "-File", ".\tools\winui_xaml_min_repro.ps1",
        "-Configuration", $Configuration,
        "-SummaryPath", $minReproSummaryPath,
        "-SummaryJsonPath", $minReproSummaryJsonPath
    )
    if ($NoRestore) {
        $minReproArgs += "-NoRestore"
    }
    & powershell @minReproArgs
    $steps.Add([ordered]@{
        name = "winui_xaml_min_repro"
        exit_code = $LASTEXITCODE
    })
}
finally {
    Pop-Location
}

$manifestPaths = @()
$candidateManifestPaths = @(
    ".\build\reports\winui\winui_diagnostic_manifest_local.json",
    ".\build\reports\winui\winui_diagnostic_manifest_windows-latest.json",
    ".\build\reports\winui\winui_diagnostic_manifest_windows-2022.json",
    ".\build\reports\winui\winui_diagnostic_manifest.json"
)
foreach ($candidate in $candidateManifestPaths) {
    $resolved = Resolve-AbsolutePath -Path $candidate -BaseDirectory $repoRoot
    if (Test-Path -LiteralPath $resolved) {
        $manifestPaths += $resolved
    }
}
$matrixSummaryPath = Join-Path $reportDir "winui_manifest_matrix_summary_latest.txt"
$matrixSummaryJsonPath = Join-Path $reportDir "winui_manifest_matrix_summary_latest.json"
if ($manifestPaths.Count -gt 0) {
    $matrixArgs = @(
        "-ExecutionPolicy", "Bypass",
        "-File", ".\tools\winui_diag_matrix_summary.ps1",
        "-OutputPath", $matrixSummaryPath,
        "-OutputJsonPath", $matrixSummaryJsonPath,
        "-ManifestPaths"
    ) + $manifestPaths
    Push-Location $repoRoot
    try {
        & powershell @matrixArgs
        $steps.Add([ordered]@{
            name = "winui_diag_matrix_summary"
            exit_code = $LASTEXITCODE
        })
    }
    finally {
        Pop-Location
    }
}

$minReproSummary = Join-Path $reportDir "winui_xaml_min_repro_summary.txt"
$failureClass = Get-KeyValueFromSummary -Path $minReproSummary -Key "FailureClass"
$wmc9999Count = Get-KeyValueFromSummary -Path $minReproSummary -Key "WMC9999Count"
$firstDiagnostic = Get-KeyValueFromSummary -Path $minReproSummary -Key "FirstDiagnostic"

$lines = [System.Collections.Generic.List[string]]::new()
$lines.Add("WinUI Blocker Triage Summary")
$lines.Add("Generated: $(Get-Date -Format o)")
$lines.Add("Configuration: $Configuration")
$lines.Add("NoRestore: $NoRestore")
$lines.Add("RunPublishHosts: $RunPublishHosts")
$lines.Add("FailureClass: $failureClass")
$lines.Add("WMC9999Count: $wmc9999Count")
$lines.Add("FirstDiagnostic: $firstDiagnostic")
$lines.Add("MinReproSummary: $minReproSummary")
$lines.Add("MatrixSummary: $matrixSummaryPath")
$lines.Add("")
$lines.Add("Steps:")
foreach ($step in $steps) {
    $lines.Add("- $($step.name): exit=$($step.exit_code)")
}
$lines | Set-Content -Path $resolvedSummary -Encoding UTF8

$json = [ordered]@{
    generated_at_utc = (Get-Date).ToUniversalTime().ToString("o")
    configuration = $Configuration
    no_restore = [bool]$NoRestore
    run_publish_hosts = [bool]$RunPublishHosts
    failure_class = $failureClass
    wmc9999_count = if ([string]::IsNullOrWhiteSpace($wmc9999Count)) { 0 } else { [int]$wmc9999Count }
    first_diagnostic = $firstDiagnostic
    min_repro_summary = $minReproSummary
    min_repro_summary_json = (Join-Path $reportDir "winui_xaml_min_repro_summary.json")
    matrix_summary = $matrixSummaryPath
    matrix_summary_json = $matrixSummaryJsonPath
    steps = @($steps)
}
$json | ConvertTo-Json -Depth 6 | Set-Content -Path $resolvedSummaryJson -Encoding UTF8

Write-Host "summary=$resolvedSummary"
Write-Host "json=$resolvedSummaryJson"

