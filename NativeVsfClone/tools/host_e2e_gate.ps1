param(
    [string]$Configuration = "Release",
    [string]$RuntimeIdentifier = "win-x64",
    [switch]$IncludeWinUi,
    [switch]$SkipNativeBuild,
    [switch]$NoRestore,
    [string]$SummaryPath = ".\build\reports\host_e2e_gate_summary.txt"
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
$resolvedSummary = Resolve-AbsolutePath -Path $SummaryPath -BaseDirectory $repoRoot
New-Item -ItemType Directory -Force -Path (Split-Path -Parent $resolvedSummary) | Out-Null

$started = Get-Date
$steps = [System.Collections.Generic.List[string]]::new()
$overall = $true

function Run-Step {
    param([string]$Name, [scriptblock]$Action)
    Write-Host "[host-e2e] START: $Name"
    try {
        & $Action
        $exitCode = $LASTEXITCODE
        if ($exitCode -ne 0) {
            $script:overall = $false
            $steps.Add("- ${Name}: FAIL (exit=$exitCode)")
            throw "$Name failed with exit=$exitCode"
        }
        $steps.Add("- ${Name}: PASS (exit=0)")
        Write-Host "[host-e2e] PASS: $Name"
    }
    catch {
        $script:overall = $false
        if (-not ($steps | Where-Object { $_ -like "- ${Name}: FAIL*" })) {
            $steps.Add("- ${Name}: FAIL (exception)")
        }
        throw
    }
}

try {
    Run-Step "Sidecar lock guard" {
        & powershell -ExecutionPolicy Bypass -File .\tools\sidecar_lock_guard.ps1
    }

    Run-Step "Host publish and smoke" {
        $scriptPath = Join-Path $repoRoot "tools\publish_hosts.ps1"
        if ($IncludeWinUi) {
            & $scriptPath -Configuration $Configuration -RuntimeIdentifier $RuntimeIdentifier -RunWpfLaunchSmoke $true -WpfLaunchSmokeFailOnError $true -IncludeWinUi -SkipNativeBuild:$SkipNativeBuild -NoRestore:$NoRestore
        } else {
            & $scriptPath -Configuration $Configuration -RuntimeIdentifier $RuntimeIdentifier -RunWpfLaunchSmoke $true -WpfLaunchSmokeFailOnError $true -SkipNativeBuild:$SkipNativeBuild -NoRestore:$NoRestore
        }
    }

    Run-Step "Release dashboard refresh" {
        & powershell -ExecutionPolicy Bypass -File .\tools\release_gate_dashboard.ps1
    }
}
catch {
    # failure already recorded in steps
}

$duration = [Math]::Round(((Get-Date) - $started).TotalSeconds, 3)
$lines = [System.Collections.Generic.List[string]]::new()
$lines.Add("Host E2E Gate Summary")
$lines.Add("Generated: $(Get-Date -Format o)")
$lines.Add("Configuration: $Configuration")
$lines.Add("RuntimeIdentifier: $RuntimeIdentifier")
$lines.Add("IncludeWinUi: $IncludeWinUi")
$lines.Add("SkipNativeBuild: $SkipNativeBuild")
$lines.Add("NoRestore: $NoRestore")
$lines.Add("DurationSec: $duration")
$lines.Add("")
$lines.Add("Steps:")
foreach ($s in $steps) { $lines.Add($s) }
$lines.Add("")
$lines.Add("Overall: $(if ($overall) { 'PASS' } else { 'FAIL' })")
$lines | Set-Content -Path $resolvedSummary -Encoding UTF8
Write-Host "summary=$resolvedSummary"

if (-not $overall) { exit 1 }
