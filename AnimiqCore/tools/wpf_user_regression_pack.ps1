param(
    [string]$Configuration = "Release",
    [string]$RuntimeIdentifier = "win-x64",
    [switch]$SkipNativeBuild,
    [switch]$NoRestore,
    [string]$OutputTxt = ".\build\reports\wpf_user_regression_pack_summary.txt"
)

$ErrorActionPreference = "Stop"

function Resolve-AbsolutePath {
    param([string]$Path, [string]$BaseDirectory)
    if ([System.IO.Path]::IsPathRooted($Path)) {
        return [System.IO.Path]::GetFullPath($Path)
    }
    return [System.IO.Path]::GetFullPath((Join-Path $BaseDirectory $Path))
}

function Invoke-Step {
    param([string]$Name, [scriptblock]$Action)
    & $Action
    return [ordered]@{
        name = $Name
        exit_code = [int]$LASTEXITCODE
        status = if ($LASTEXITCODE -eq 0) { "PASS" } else { "FAIL" }
    }
}

$repoRoot = Split-Path -Parent $PSScriptRoot
$resolvedTxt = Resolve-AbsolutePath -Path $OutputTxt -BaseDirectory $repoRoot
New-Item -ItemType Directory -Force -Path (Split-Path -Parent $resolvedTxt) | Out-Null

$rows = @()
Push-Location $repoRoot
try {
    $rows += Invoke-Step -Name "WPF publish + launch smoke" -Action {
        $args = @(
            "-ExecutionPolicy", "Bypass",
            "-File", ".\tools\publish_hosts.ps1",
            "-Configuration", $Configuration,
            "-RuntimeIdentifier", $RuntimeIdentifier,
            "-RunWpfLaunchSmoke", "$true",
            "-WpfLaunchSmokeFailOnError", "$true"
        )
        if ($SkipNativeBuild) { $args += "-SkipNativeBuild" }
        if ($NoRestore) { $args += "-NoRestore" }
        & powershell @args
    }

    $rows += Invoke-Step -Name "Host E2E gate (WPF track)" -Action {
        $args = @("-ExecutionPolicy", "Bypass", "-File", ".\tools\host_e2e_gate.ps1")
        if ($SkipNativeBuild) { $args += "-SkipNativeBuild" }
        if ($NoRestore) { $args += "-NoRestore" }
        & powershell @args
    }

    $rows += Invoke-Step -Name "Tracking parser fuzz" -Action {
        $args = @("-ExecutionPolicy", "Bypass", "-File", ".\tools\tracking_parser_fuzz_gate.ps1")
        if ($NoRestore) { $args += "-NoRestore" }
        & powershell @args
    }

    $rows += Invoke-Step -Name "Release dashboard refresh" -Action {
        & powershell -ExecutionPolicy Bypass -File .\tools\release_gate_dashboard.ps1
    }
}
finally {
    Pop-Location
}

$overallPass = (@($rows | Where-Object { $_.status -eq "FAIL" }).Count -eq 0)
$lines = [System.Collections.Generic.List[string]]::new()
$lines.Add("WPF User Regression Pack Summary")
$lines.Add("GeneratedUtc: $((Get-Date).ToUniversalTime().ToString('o'))")
$lines.Add("Configuration: $Configuration")
$lines.Add("RuntimeIdentifier: $RuntimeIdentifier")
$lines.Add("NoRestore: $NoRestore")
$lines.Add("SkipNativeBuild: $SkipNativeBuild")
foreach ($row in $rows) {
    $lines.Add("- $($row.name): $($row.status) (exit=$($row.exit_code))")
}
$lines.Add("Overall: $(if ($overallPass) { 'PASS' } else { 'FAIL' })")
$lines | Set-Content -Path $resolvedTxt -Encoding UTF8
Write-Host "summary=$resolvedTxt"

if (-not $overallPass) { exit 1 }

