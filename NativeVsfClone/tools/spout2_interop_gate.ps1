param(
    [string]$Configuration = "Release",
    [string]$RuntimeIdentifier = "win-x64",
    [switch]$SkipNativeBuild,
    [switch]$NoRestore,
    [switch]$IncludeWinUi,
    [switch]$SkipHostE2E,
    [switch]$RequireSpout2Configured,
    [string]$SummaryPath = ".\build\reports\spout2_interop_gate_summary.txt"
)

$ErrorActionPreference = "Stop"

function Resolve-AbsolutePath {
    param([string]$Path, [string]$BaseDirectory)
    if ([System.IO.Path]::IsPathRooted($Path)) {
        return [System.IO.Path]::GetFullPath($Path)
    }
    return [System.IO.Path]::GetFullPath((Join-Path $BaseDirectory $Path))
}

function Read-CMakeFlag {
    param([string]$CachePath, [string]$Name)
    if (-not (Test-Path $CachePath)) { return "" }
    $line = Get-Content -Path $CachePath | Where-Object { $_ -like "${Name}:*" } | Select-Object -First 1
    if ([string]::IsNullOrWhiteSpace($line)) { return "" }
    $parts = $line.Split("=")
    if ($parts.Count -lt 2) { return "" }
    return $parts[1].Trim()
}

$repoRoot = Split-Path -Parent $PSScriptRoot
$resolvedSummary = Resolve-AbsolutePath -Path $SummaryPath -BaseDirectory $repoRoot
New-Item -ItemType Directory -Force -Path (Split-Path -Parent $resolvedSummary) | Out-Null

$started = Get-Date
$steps = [System.Collections.Generic.List[string]]::new()
$overall = $true

$spoutIncludeDir = Join-Path $repoRoot "third_party\Spout2\include"
$spoutSdkDetected = Test-Path $spoutIncludeDir
$steps.Add("- Spout2 SDK include detected: $(if ($spoutSdkDetected) { 'YES' } else { 'NO' }) ($spoutIncludeDir)")

$cachePath = Join-Path $repoRoot "build\CMakeCache.txt"
$cmakeSpoutFlag = Read-CMakeFlag -CachePath $cachePath -Name "VSFCLONE_ENABLE_SPOUT2"
if ([string]::IsNullOrWhiteSpace($cmakeSpoutFlag)) {
    $cmakeSpoutFlag = "UNKNOWN"
}
$steps.Add("- CMake flag VSFCLONE_ENABLE_SPOUT2: $cmakeSpoutFlag")

if ($RequireSpout2Configured -and -not $spoutSdkDetected) {
    $overall = $false
    $steps.Add("- Spout2 configuration contract: FAIL (SDK include path missing)")
} else {
    $steps.Add("- Spout2 configuration contract: PASS")
}

if (-not $SkipHostE2E) {
    Write-Host "[spout2-gate] START: Host E2E gate"
    $hostArgs = @(
        "-ExecutionPolicy", "Bypass",
        "-File", ".\tools\host_e2e_gate.ps1",
        "-Configuration", $Configuration,
        "-RuntimeIdentifier", $RuntimeIdentifier
    )
    if ($SkipNativeBuild) { $hostArgs += "-SkipNativeBuild" }
    if ($NoRestore) { $hostArgs += "-NoRestore" }
    if ($IncludeWinUi) { $hostArgs += "-IncludeWinUi" }

    Push-Location $repoRoot
    try {
        & powershell @hostArgs
        if ($LASTEXITCODE -ne 0) {
            $overall = $false
            $steps.Add("- Host E2E gate: FAIL (exit=$LASTEXITCODE)")
        } else {
            $steps.Add("- Host E2E gate: PASS (exit=0)")
        }
    }
    finally {
        Pop-Location
    }
} else {
    $steps.Add("- Host E2E gate: SKIPPED")
}

$duration = [Math]::Round(((Get-Date) - $started).TotalSeconds, 3)
$lines = [System.Collections.Generic.List[string]]::new()
$lines.Add("Spout2 Interop Gate Summary")
$lines.Add("Generated: $(Get-Date -Format o)")
$lines.Add("Configuration: $Configuration")
$lines.Add("RuntimeIdentifier: $RuntimeIdentifier")
$lines.Add("SkipNativeBuild: $SkipNativeBuild")
$lines.Add("NoRestore: $NoRestore")
$lines.Add("IncludeWinUi: $IncludeWinUi")
$lines.Add("SkipHostE2E: $SkipHostE2E")
$lines.Add("RequireSpout2Configured: $RequireSpout2Configured")
$lines.Add("DurationSec: $duration")
$lines.Add("")
$lines.Add("Steps:")
foreach ($s in $steps) { $lines.Add($s) }
$lines.Add("")
$lines.Add("Artifacts:")
$lines.Add("- build/reports/host_e2e_gate_summary.txt")
$lines.Add("- build/reports/release_gate_dashboard.txt")
$lines.Add("- build/reports/spout2_interop_gate_summary.txt")
$lines.Add("")
$lines.Add("Overall: $(if ($overall) { 'PASS' } else { 'FAIL' })")
$lines | Set-Content -Path $resolvedSummary -Encoding UTF8
Write-Host "summary=$resolvedSummary"

if (-not $overall) { exit 1 }
