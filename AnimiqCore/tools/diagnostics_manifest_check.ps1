param(
    [string]$ManifestPath = "",
    [string]$BundleDir = "",
    [string]$OutputTxt = ".\build\reports\diagnostics_manifest_check_summary.txt",
    [string]$OutputJson = ".\build\reports\diagnostics_manifest_check_summary.json"
)

$ErrorActionPreference = "Stop"

function Resolve-AbsolutePath {
    param([string]$Path, [string]$BaseDirectory)
    if ([System.IO.Path]::IsPathRooted($Path)) {
        return [System.IO.Path]::GetFullPath($Path)
    }
    return [System.IO.Path]::GetFullPath((Join-Path $BaseDirectory $Path))
}

function Compute-FileSha256 {
    param([string]$Path)
    if (-not (Test-Path -LiteralPath $Path)) {
        return ""
    }
    $hash = Get-FileHash -Algorithm SHA256 -LiteralPath $Path
    return $hash.Hash.ToUpperInvariant()
}

$repoRoot = Split-Path -Parent $PSScriptRoot
$resolvedOutputTxt = Resolve-AbsolutePath -Path $OutputTxt -BaseDirectory $repoRoot
$resolvedOutputJson = Resolve-AbsolutePath -Path $OutputJson -BaseDirectory $repoRoot
New-Item -ItemType Directory -Force -Path (Split-Path -Parent $resolvedOutputTxt) | Out-Null
New-Item -ItemType Directory -Force -Path (Split-Path -Parent $resolvedOutputJson) | Out-Null

$resolvedManifestPath = ""
if (-not [string]::IsNullOrWhiteSpace($ManifestPath)) {
    $resolvedManifestPath = Resolve-AbsolutePath -Path $ManifestPath -BaseDirectory $repoRoot
} elseif (-not [string]::IsNullOrWhiteSpace($BundleDir)) {
    $resolvedBundleDir = Resolve-AbsolutePath -Path $BundleDir -BaseDirectory $repoRoot
    $resolvedManifestPath = Join-Path $resolvedBundleDir "diagnostics_manifest.json"
} else {
    throw "Provide ManifestPath or BundleDir."
}

if (-not (Test-Path -LiteralPath $resolvedManifestPath)) {
    throw "diagnostics manifest missing: $resolvedManifestPath"
}

$manifestDir = Split-Path -Parent $resolvedManifestPath
$manifest = Get-Content -Raw -Path $resolvedManifestPath | ConvertFrom-Json

$checks = [System.Collections.Generic.List[object]]::new()
function Add-Check {
    param([string]$Name, [bool]$Pass, [string]$Detail)
    $checks.Add([PSCustomObject]@{ name = $Name; pass = $Pass; detail = $Detail })
}

$manifestVersion = "$($manifest.manifest_version)"
$gateContractVersion = "$($manifest.gate_contract_version)"
$metricsSessionId = "$($manifest.session.metrics_session_id)"

Add-Check -Name "manifest_version_present" -Pass (-not [string]::IsNullOrWhiteSpace($manifestVersion)) -Detail $manifestVersion
Add-Check -Name "gate_contract_version_present" -Pass (-not [string]::IsNullOrWhiteSpace($gateContractVersion)) -Detail $gateContractVersion
Add-Check -Name "metrics_session_id_present" -Pass (-not [string]::IsNullOrWhiteSpace($metricsSessionId)) -Detail $metricsSessionId

$fileKeyMap = @(
    @{ key = "telemetry_sha256"; file = "telemetry.json" },
    @{ key = "snapshot_sha256"; file = "snapshot.json" },
    @{ key = "preflight_sha256"; file = "preflight.json" },
    @{ key = "environment_sha256"; file = "environment_snapshot.json" },
    @{ key = "repro_commands_sha256"; file = "repro_commands.txt" },
    @{ key = "onboarding_kpi_sha256"; file = "onboarding_kpi_summary.txt" }
)

foreach ($item in $fileKeyMap) {
    $expected = "$($manifest.files.($item.key))".Trim().ToUpperInvariant()
    $filePath = Join-Path $manifestDir $item.file
    $actual = Compute-FileSha256 -Path $filePath
    $hasExpected = -not [string]::IsNullOrWhiteSpace($expected)
    $fileExists = Test-Path -LiteralPath $filePath
    $match = $hasExpected -and $fileExists -and [string]::Equals($expected, $actual, [System.StringComparison]::OrdinalIgnoreCase)
    Add-Check -Name "sha_match_$($item.file)" -Pass $match -Detail "expected=$expected, actual=$actual, exists=$fileExists"
}

$overall = ($checks | Where-Object { -not $_.pass }).Count -eq 0

$summary = [PSCustomObject]@{
    generated_utc = (Get-Date).ToUniversalTime().ToString("s")
    manifest_path = $resolvedManifestPath
    overall = if ($overall) { "PASS" } else { "FAIL" }
    checks = $checks
}

$lines = [System.Collections.Generic.List[string]]::new()
$lines.Add("Diagnostics Manifest Check Summary")
$lines.Add("GeneratedUTC: $($summary.generated_utc)")
$lines.Add("ManifestPath: $resolvedManifestPath")
$lines.Add("Overall: $($summary.overall)")
$lines.Add("")
foreach ($check in $checks) {
    $lines.Add("- $($check.name): $(if ($check.pass) { 'PASS' } else { 'FAIL' }) | $($check.detail)")
}

$lines | Set-Content -Path $resolvedOutputTxt -Encoding UTF8
$summary | ConvertTo-Json -Depth 5 | Set-Content -Path $resolvedOutputJson -Encoding UTF8

Write-Host "summary_txt=$resolvedOutputTxt"
Write-Host "summary_json=$resolvedOutputJson"

if (-not $overall) {
    exit 1
}
