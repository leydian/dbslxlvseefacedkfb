param(
    [string]$Configuration = "Release",
    [string]$RuntimeIdentifier = "win-x64",
    [switch]$SkipNativeBuild,
    [switch]$NoRestore,
    [string]$OutputTxt = ".\build\reports\release_dual_lane_gate_summary.txt"
)

$ErrorActionPreference = "Stop"

function Resolve-AbsolutePath {
    param([string]$Path, [string]$BaseDirectory)
    if ([System.IO.Path]::IsPathRooted($Path)) {
        return [System.IO.Path]::GetFullPath($Path)
    }
    return [System.IO.Path]::GetFullPath((Join-Path $BaseDirectory $Path))
}

function Invoke-Lane {
    param([string]$LaneName, [switch]$IncludeWinUi)
    $args = @(
        "-ExecutionPolicy", "Bypass",
        "-File", ".\tools\release_readiness_gate.ps1",
        "-Configuration", $Configuration,
        "-RuntimeIdentifier", $RuntimeIdentifier
    )
    if ($SkipNativeBuild) { $args += "-SkipNativeBuild" }
    if ($NoRestore) { $args += "-NoRestore" }
    if ($IncludeWinUi) { $args += "-IncludeWinUi" }

    & powershell @args
    return [ordered]@{
        lane = $LaneName
        include_winui = [bool]$IncludeWinUi
        exit_code = [int]$LASTEXITCODE
        status = if ($LASTEXITCODE -eq 0) { "PASS" } else { "FAIL" }
    }
}

$repoRoot = Split-Path -Parent $PSScriptRoot
$resolvedTxt = Resolve-AbsolutePath -Path $OutputTxt -BaseDirectory $repoRoot
New-Item -ItemType Directory -Force -Path (Split-Path -Parent $resolvedTxt) | Out-Null

Push-Location $repoRoot
try {
    $wpf = Invoke-Lane -LaneName "WPF_ONLY" -IncludeWinUi:$false
    $full = Invoke-Lane -LaneName "FULL" -IncludeWinUi:$true
}
finally {
    Pop-Location
}

$lines = @(
    "Release Dual Lane Gate Summary"
    "GeneratedUtc: $((Get-Date).ToUniversalTime().ToString('o'))"
    "- $($wpf.lane): $($wpf.status) (exit=$($wpf.exit_code), include_winui=$($wpf.include_winui))"
    "- $($full.lane): $($full.status) (exit=$($full.exit_code), include_winui=$($full.include_winui))"
    "Overall: $(if ($wpf.exit_code -eq 0 -and $full.exit_code -eq 0) { 'PASS' } else { 'FAIL' })"
)
$lines | Set-Content -Path $resolvedTxt -Encoding UTF8
Write-Host "summary=$resolvedTxt"

if ($wpf.exit_code -ne 0 -or $full.exit_code -ne 0) {
    exit 1
}

