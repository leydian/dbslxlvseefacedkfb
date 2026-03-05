param(
    [string]$Configuration = "Release",
    [string]$RuntimeIdentifier = "win-x64",
    [switch]$SkipNativeBuild,
    [switch]$IncludeWinUi,
    [switch]$NoRestore,
    [switch]$EnableHostE2E,
    [switch]$EnableWinUiMinRepro,
    [switch]$EnableNuGetMirrorBootstrap,
    [switch]$EnableMediapipeSanity,
    [switch]$EnableSpout2Interop,
    [switch]$EnableXav2CompressionQuality,
    [switch]$EnableXav2Parity,
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
            $args = @(
                "-ExecutionPolicy", "Bypass",
                "-File", ".\tools\run_quality_baseline.ps1"
            )
            if ($EnableXav2CompressionQuality) { $args += "-EnableXav2CompressionQuality" }
            if ($EnableXav2Parity) { $args += "-EnableXav2Parity" }
            if ($EnableSpout2Interop) { $args += "-EnableSpout2Interop" }
            & powershell @args
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

    if ($EnableNuGetMirrorBootstrap) {
        $results.Add((Invoke-Step -Name "NuGet mirror bootstrap" -Action {
            & powershell -ExecutionPolicy Bypass -File .\tools\nuget_mirror_bootstrap.ps1
        }))
    }

    if ($EnableMediapipeSanity) {
        $results.Add((Invoke-Step -Name "MediaPipe sidecar sanity" -Action {
            & powershell -ExecutionPolicy Bypass -File .\tools\mediapipe_sidecar_sanity.ps1
        }))
    }

    if ($EnableHostE2E) {
        $results.Add((Invoke-Step -Name "Host E2E gate" -Action {
            $args = @(
                "-ExecutionPolicy", "Bypass",
                "-File", ".\tools\host_e2e_gate.ps1",
                "-Configuration", $Configuration,
                "-RuntimeIdentifier", $RuntimeIdentifier
            )
            if ($IncludeWinUi) { $args += "-IncludeWinUi" }
            if ($SkipNativeBuild) { $args += "-SkipNativeBuild" }
            if ($NoRestore) { $args += "-NoRestore" }
            & powershell @args
        }))
    }

    if ($EnableWinUiMinRepro -and $IncludeWinUi) {
        $results.Add((Invoke-Step -Name "WinUI XAML minimal repro" -Action {
            $args = @(
                "-ExecutionPolicy", "Bypass",
                "-File", ".\tools\winui_xaml_min_repro.ps1",
                "-Configuration", $Configuration
            )
            if ($NoRestore) { $args += "-NoRestore" }
            & powershell @args
        }))
    }
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
$lines.Add("EnableHostE2E: $EnableHostE2E")
$lines.Add("EnableWinUiMinRepro: $EnableWinUiMinRepro")
$lines.Add("EnableNuGetMirrorBootstrap: $EnableNuGetMirrorBootstrap")
$lines.Add("EnableMediapipeSanity: $EnableMediapipeSanity")
$lines.Add("EnableSpout2Interop: $EnableSpout2Interop")
$lines.Add("EnableXav2CompressionQuality: $EnableXav2CompressionQuality")
$lines.Add("EnableXav2Parity: $EnableXav2Parity")
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
$lines.Add("- build/reports/host_e2e_gate_summary.txt")
$lines.Add("- build/reports/winui_xaml_min_repro_summary.txt")
$lines.Add("- build/reports/nuget_mirror_bootstrap_summary.txt")
$lines.Add("- build/reports/mediapipe_sidecar_sanity_summary.txt")
$lines.Add("- build/reports/spout2_interop_gate_summary.txt")
$lines.Add("- build/reports/xav2_compression_quality_gate_summary.txt")
$lines.Add("- build/reports/xav2_parity_gate_summary.txt")

$lines | Set-Content -Path $resolvedSummaryPath -Encoding UTF8
Write-Host "[release-readiness] Summary: $resolvedSummaryPath"
