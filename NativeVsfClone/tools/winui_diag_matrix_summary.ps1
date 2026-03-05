param(
    [Parameter(Mandatory = $true)][string[]]$ManifestPaths,
    [string]$OutputPath = ".\build\reports\winui_manifest_matrix_summary_latest.txt"
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
New-Item -ItemType Directory -Force -Path (Split-Path -Parent $resolvedOutput) | Out-Null

$rows = [System.Collections.Generic.List[object]]::new()
foreach ($path in $ManifestPaths) {
    $resolved = Resolve-AbsolutePath -Path $path -BaseDirectory $repoRoot
    if (-not (Test-Path $resolved)) {
        throw "Manifest not found: $resolved"
    }
    $manifest = Get-Content -Raw -Path $resolved | ConvertFrom-Json
    $rows.Add([ordered]@{
        path = $resolved
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
$lines.Add("")
$lines.Add("Rows:")

$index = 0
foreach ($r in $rows) {
    $index++
    $lines.Add("- [$index] path: $($r.path)")
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
