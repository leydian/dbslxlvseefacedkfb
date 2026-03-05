param(
    [string]$Configuration = "Release",
    [string]$RuntimeIdentifier = "win-x64",
    [switch]$SkipNativeBuild,
    [switch]$IncludeWinUi,
    [switch]$NoRestore,
    [switch]$SkipVersionContractCheck,
    [switch]$SkipQualityBaseline,
    [string]$SummaryPath = ".\build\reports\release_readiness_gate_summary.txt"
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
    param(
        [Parameter(Mandatory = $true)][string]$Name,
        [Parameter(Mandatory = $true)][scriptblock]$Action
    )

    Write-Host "[release-readiness] START: $Name"
    & $Action
    $exitCode = $LASTEXITCODE
    if ($exitCode -ne 0) {
        throw "$Name failed (exit=$exitCode)."
    }
    Write-Host "[release-readiness] PASS: $Name"
    return [PSCustomObject]@{
        Name = $Name
        Status = "PASS"
        ExitCode = $exitCode
    }
}

$repoRoot = Split-Path -Parent $PSScriptRoot
$resolvedSummaryPath = Resolve-AbsolutePath -Path $SummaryPath -BaseDirectory $repoRoot
New-Item -ItemType Directory -Force -Path (Split-Path -Parent $resolvedSummaryPath) | Out-Null

$results = [System.Collections.Generic.List[object]]::new()
$started = Get-Date

Push-Location $repoRoot
try {
    if (-not $SkipVersionContractCheck) {
        $results.Add((Invoke-Step -Name "Version contract check" -Action {
            & powershell -ExecutionPolicy Bypass -File .\tools\version_contract_check.ps1
        }))
    } else {
        Write-Host "[release-readiness] SKIP: Version contract check"
    }

    if (-not $SkipQualityBaseline) {
        $results.Add((Invoke-Step -Name "Quality baseline gates" -Action {
            & powershell -ExecutionPolicy Bypass -File .\tools\run_quality_baseline.ps1
        }))
    } else {
        Write-Host "[release-readiness] SKIP: Quality baseline gates"
    }

    $results.Add((Invoke-Step -Name "Host publish (WPF-first)" -Action {
        $args = @(
            "-ExecutionPolicy", "Bypass",
            "-File", ".\tools\publish_hosts.ps1",
            "-Configuration", $Configuration,
            "-RuntimeIdentifier", $RuntimeIdentifier
        )
        if ($SkipNativeBuild) { $args += "-SkipNativeBuild" }
        if ($IncludeWinUi) { $args += "-IncludeWinUi" }
        if ($NoRestore) { $args += "-NoRestore" }
        & powershell @args
    }))

    $results.Add((Invoke-Step -Name "Release gate dashboard refresh" -Action {
        & powershell -ExecutionPolicy Bypass -File .\tools\release_gate_dashboard.ps1
    }))
}
finally {
    Pop-Location
}

$durationSec = [int]((Get-Date) - $started).TotalSeconds
$lines = [System.Collections.Generic.List[string]]::new()
$lines.Add("Release Readiness Gate Summary")
$lines.Add("Generated: $(Get-Date -Format o)")
$lines.Add("Configuration: $Configuration")
$lines.Add("RuntimeIdentifier: $RuntimeIdentifier")
$lines.Add("IncludeWinUi: $IncludeWinUi")
$lines.Add("NoRestore: $NoRestore")
$lines.Add("SkipNativeBuild: $SkipNativeBuild")
$lines.Add("DurationSec: $durationSec")
$lines.Add("")
$lines.Add("Steps:")
foreach ($r in $results) {
    $lines.Add("- $($r.Name): $($r.Status) (exit=$($r.ExitCode))")
}
$lines.Add("")
$lines.Add("Artifacts:")
$lines.Add("- build/reports/quality_baseline_summary.txt")
$lines.Add("- build/reports/host_publish_latest.txt")
$lines.Add("- build/reports/release_gate_dashboard.txt")
$lines.Add("- build/reports/release_gate_dashboard.json")

$lines | Set-Content -Path $resolvedSummaryPath -Encoding UTF8
Write-Host "[release-readiness] Summary: $resolvedSummaryPath"
