param(
    [string[]]$ManifestPaths,
    [string]$OutputPath = ".\build\reports\winui_manifest_matrix_summary_latest.txt",
    [string]$OutputJsonPath = ".\build\reports\winui_manifest_matrix_summary_latest.json"
)

$ErrorActionPreference = "Stop"

function Resolve-AbsolutePath {
    param([string]$Path, [string]$BaseDirectory)
    if ([System.IO.Path]::IsPathRooted($Path)) {
        return [System.IO.Path]::GetFullPath($Path)
    }
    return [System.IO.Path]::GetFullPath((Join-Path $BaseDirectory $Path))
}

function Get-ManifestValue {
    param([object]$Manifest, [string]$Name)
    if ($null -eq $Manifest) { return "" }
    $raw = $Manifest.$Name
    if ($null -eq $raw) { return "" }
    return "$raw".Trim()
}

$repoRoot = Split-Path -Parent $PSScriptRoot
$resolvedOutput = Resolve-AbsolutePath -Path $OutputPath -BaseDirectory $repoRoot
if ([string]::IsNullOrWhiteSpace($OutputJsonPath)) {
    $OutputJsonPath = ".\build\reports\winui_manifest_matrix_summary_latest.json"
}
$resolvedOutputJson = Resolve-AbsolutePath -Path $OutputJsonPath -BaseDirectory $repoRoot
New-Item -ItemType Directory -Force -Path (Split-Path -Parent $resolvedOutput) | Out-Null
New-Item -ItemType Directory -Force -Path (Split-Path -Parent $resolvedOutputJson) | Out-Null

if ($null -eq $ManifestPaths -or $ManifestPaths.Count -eq 0) {
    $defaultCandidates = @(
        ".\build\reports\winui\winui_diagnostic_manifest.json",
        ".\build\reports\winui\winui_diagnostic_manifest_local.json",
        ".\build\reports\winui\winui_diagnostic_manifest_windows-latest.json",
        ".\build\reports\winui\winui_diagnostic_manifest_windows-2022.json"
    )
    $ManifestPaths = @()
    foreach ($candidate in $defaultCandidates) {
        $resolvedCandidate = Resolve-AbsolutePath -Path $candidate -BaseDirectory $repoRoot
        if (Test-Path -LiteralPath $resolvedCandidate) {
            $ManifestPaths += $candidate
        }
    }
    if ($ManifestPaths.Count -eq 0) {
        throw "No manifest paths were provided and no default manifests were found."
    }
}

$rows = [System.Collections.Generic.List[object]]::new()
foreach ($path in $ManifestPaths) {
    $resolved = Resolve-AbsolutePath -Path $path -BaseDirectory $repoRoot
    if (-not (Test-Path $resolved)) {
        throw "Manifest not found: $resolved"
    }
    $manifest = Get-Content -Raw -Path $resolved | ConvertFrom-Json
    $lane = "unknown"
    if ($resolved -match '(windows-latest|windows-2022|local)') {
        $lane = $matches[1]
    }
    $rows.Add([ordered]@{
        path = $resolved
        lane = $lane
        generated_at_utc = Get-ManifestValue -Manifest $manifest -Name "generated_at_utc"
        failure_class = Get-ManifestValue -Manifest $manifest -Name "failure_class"
        failure_class_confidence = Get-ManifestValue -Manifest $manifest -Name "failure_class_confidence"
        reason = Get-ManifestValue -Manifest $manifest -Name "reason"
        preflight_passed = "$($manifest.preflight.passed)"
        failed_checks = (@($manifest.preflight.failed_checks) -join ",")
        root_cause_hints = (@($manifest.root_cause_hints) -join " | ")
        dotnet_version = Get-ManifestValue -Manifest $manifest -Name "dotnet_version"
    })
}

$uniqueClasses = @($rows | ForEach-Object { $_.failure_class } | Where-Object { -not [string]::IsNullOrWhiteSpace($_) } | Sort-Object -Unique)
$classConverged = ($uniqueClasses.Count -eq 1)

$lines = [System.Collections.Generic.List[string]]::new()
$lines.Add("WinUI Diagnostic Manifest Matrix Summary")
$lines.Add("Generated: $(Get-Date -Format o)")
$lines.Add("ManifestCount: $($rows.Count)")
$lines.Add("FailureClassConverged: $classConverged")
$lines.Add("FailureClasses: $(if ($uniqueClasses.Count -eq 0) { '<none>' } else { $uniqueClasses -join ',' })")
$laneCounts = $rows | Group-Object -Property lane | Sort-Object -Property Name
if ($laneCounts.Count -gt 0) {
    $laneSummary = @($laneCounts | ForEach-Object {
        $name = if ([string]::IsNullOrWhiteSpace($_.Name)) { "unknown" } else { $_.Name }
        "{0}={1}" -f $name, $_.Count
    })
    $lines.Add("Lanes: $([string]::Join(', ', $laneSummary))")
}
$lines.Add("")
$lines.Add("Rows:")

$index = 0
foreach ($r in $rows) {
    $index++
    $lines.Add("- [$index] path: $($r.path)")
    $lines.Add("  lane: $($r.lane)")
    $lines.Add("  generated_at_utc: $($r.generated_at_utc)")
    $lines.Add("  failure_class: $($r.failure_class)")
    $lines.Add("  confidence: $($r.failure_class_confidence)")
    $lines.Add("  reason: $($r.reason)")
    $lines.Add("  preflight_passed: $($r.preflight_passed)")
    $lines.Add("  failed_checks: $($r.failed_checks)")
    $lines.Add("  dotnet_version: $($r.dotnet_version)")
    $lines.Add("  root_cause_hints: $($r.root_cause_hints)")
}

$lines | Set-Content -Path $resolvedOutput -Encoding UTF8
Write-Host "Summary report written: $resolvedOutput"

$json = [ordered]@{
    generated_at_utc = (Get-Date).ToUniversalTime().ToString("o")
    manifest_count = [int]$rows.Count
    failure_class_converged = [bool]$classConverged
    failure_classes = @($uniqueClasses)
    lanes = @($laneCounts | ForEach-Object {
        [ordered]@{
            lane = $_.Name
            count = [int]$_.Count
        }
    })
    rows = @($rows)
}
$json | ConvertTo-Json -Depth 6 | Set-Content -Path $resolvedOutputJson -Encoding UTF8
Write-Host "JSON report written: $resolvedOutputJson"
