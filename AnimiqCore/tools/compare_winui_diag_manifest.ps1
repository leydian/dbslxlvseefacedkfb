param(
    [Parameter(Mandatory = $true)][string]$BaseManifestPath,
    [Parameter(Mandatory = $true)][string]$TargetManifestPath,
    [string]$OutputPath = ".\build\reports\winui_manifest_diff_latest.txt"
)

$ErrorActionPreference = "Stop"

function Resolve-AbsolutePath {
    param([string]$Path, [string]$BaseDirectory)
    if ([System.IO.Path]::IsPathRooted($Path)) {
        return [System.IO.Path]::GetFullPath($Path)
    }
    return [System.IO.Path]::GetFullPath((Join-Path $BaseDirectory $Path))
}

function To-JsonString {
    param([object]$Value)
    if ($null -eq $Value) {
        return "<null>"
    }
    return ($Value | ConvertTo-Json -Depth 10 -Compress)
}

function Add-DiffLine {
    param(
        [System.Collections.Generic.List[string]]$Lines,
        [string]$Name,
        [object]$BaseValue,
        [object]$TargetValue
    )

    $baseText = To-JsonString -Value $BaseValue
    $targetText = To-JsonString -Value $TargetValue
    if ($baseText -eq $targetText) {
        $Lines.Add("SAME   $Name = $targetText")
    } else {
        $Lines.Add("DIFF   $Name")
        $Lines.Add("  base:   $baseText")
        $Lines.Add("  target: $targetText")
    }
}

$repoRoot = Split-Path -Parent $PSScriptRoot
$resolvedBase = Resolve-AbsolutePath -Path $BaseManifestPath -BaseDirectory $repoRoot
$resolvedTarget = Resolve-AbsolutePath -Path $TargetManifestPath -BaseDirectory $repoRoot
$resolvedOutput = Resolve-AbsolutePath -Path $OutputPath -BaseDirectory $repoRoot
New-Item -ItemType Directory -Force -Path (Split-Path -Parent $resolvedOutput) | Out-Null

if (-not (Test-Path $resolvedBase)) {
    throw "Base manifest not found: $resolvedBase"
}
if (-not (Test-Path $resolvedTarget)) {
    throw "Target manifest not found: $resolvedTarget"
}

$base = Get-Content -Raw -Path $resolvedBase | ConvertFrom-Json
$target = Get-Content -Raw -Path $resolvedTarget | ConvertFrom-Json

$lines = [System.Collections.Generic.List[string]]::new()
$lines.Add("WinUI manifest diff generated: $(Get-Date -Format o)")
$lines.Add("Base: $resolvedBase")
$lines.Add("Target: $resolvedTarget")
$lines.Add("")

Add-DiffLine -Lines $lines -Name "failure_class" -BaseValue $base.failure_class -TargetValue $target.failure_class
Add-DiffLine -Lines $lines -Name "failure_class_confidence" -BaseValue $base.failure_class_confidence -TargetValue $target.failure_class_confidence
Add-DiffLine -Lines $lines -Name "reason" -BaseValue $base.reason -TargetValue $target.reason
Add-DiffLine -Lines $lines -Name "preflight.passed" -BaseValue $base.preflight.passed -TargetValue $target.preflight.passed
Add-DiffLine -Lines $lines -Name "preflight.failed_checks" -BaseValue @($base.preflight.failed_checks) -TargetValue @($target.preflight.failed_checks)
Add-DiffLine -Lines $lines -Name "preflight.warnings" -BaseValue @($base.preflight.warnings) -TargetValue @($target.preflight.warnings)
Add-DiffLine -Lines $lines -Name "root_cause_hints" -BaseValue @($base.root_cause_hints) -TargetValue @($target.root_cause_hints)
Add-DiffLine -Lines $lines -Name "nuget_probe.summary.enabled" -BaseValue $base.nuget_probe.summary.enabled -TargetValue $target.nuget_probe.summary.enabled
Add-DiffLine -Lines $lines -Name "nuget_probe.summary.reachable" -BaseValue $base.nuget_probe.summary.reachable -TargetValue $target.nuget_probe.summary.reachable
Add-DiffLine -Lines $lines -Name "nuget_probe.summary.unreachable" -BaseValue $base.nuget_probe.summary.unreachable -TargetValue $target.nuget_probe.summary.unreachable
Add-DiffLine -Lines $lines -Name "nuget_probe.summary.unknown" -BaseValue $base.nuget_probe.summary.unknown -TargetValue $target.nuget_probe.summary.unknown

$baseProfiles = @($base.profiles)
$targetProfiles = @($target.profiles)
Add-DiffLine -Lines $lines -Name "profiles.count" -BaseValue $baseProfiles.Count -TargetValue $targetProfiles.Count

$maxProfiles = [Math]::Max($baseProfiles.Count, $targetProfiles.Count)
for ($i = 0; $i -lt $maxProfiles; $i++) {
    $bp = if ($i -lt $baseProfiles.Count) { $baseProfiles[$i] } else { $null }
    $tp = if ($i -lt $targetProfiles.Count) { $targetProfiles[$i] } else { $null }
    $name = if ($null -ne $bp) { "$($bp.name)" } elseif ($null -ne $tp) { "$($tp.name)" } else { "index-$i" }
    Add-DiffLine -Lines $lines -Name "profiles[$i].name" -BaseValue ($bp.name) -TargetValue ($tp.name)
    Add-DiffLine -Lines $lines -Name "profiles[$i].enabled" -BaseValue ($bp.enabled) -TargetValue ($tp.enabled)
    Add-DiffLine -Lines $lines -Name "profiles[$i].command" -BaseValue ($bp.command) -TargetValue ($tp.command)
    Add-DiffLine -Lines $lines -Name "profiles[$i].exit_code" -BaseValue ($bp.exit_code) -TargetValue ($tp.exit_code)
    Add-DiffLine -Lines $lines -Name "profiles[$i].root_cause_hints" -BaseValue @($bp.root_cause_hints) -TargetValue @($tp.root_cause_hints)
}

$lines | Set-Content -Path $resolvedOutput -Encoding UTF8
Write-Host "Diff report written: $resolvedOutput"
