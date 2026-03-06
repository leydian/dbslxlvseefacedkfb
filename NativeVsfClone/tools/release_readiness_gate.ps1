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
    [switch]$EnableTrackingFuzz,
    [switch]$EnableHostOnboardingStateSmoke,
    [switch]$EnableUnityXav2EnvBootstrap,
    [switch]$EnableWinUiBlockerTriage,
    [switch]$EnableXav2CorpusPrep,
    [string]$Xav2CorpusSourceDir = "..",
    [string]$Xav2CorpusOutputDir = ".\build\gate_corpus\xav2",
    [int]$Xav2CorpusMinSampleCount = 10,
    [switch]$Xav2CorpusIncludeBuildArtifacts,
    [switch]$EnableSpout2Interop,
    [switch]$EnableSpout2Strict,
    [switch]$RequireSpout2StrictContract,
    [switch]$EnableUnityXav2LtsGate = $true,
    [switch]$EnableXav2CompressionQuality,
    [switch]$EnableXav2Parity,
    [ValidateSet("realtime-stable", "legacy", "aggressive", "ultra-parity", "desktop-60", "desktop-30")]
    [string]$RenderPerfProfile = "desktop-60",
    [int]$RenderPerfTargetFps = 0,
    [double]$RenderPerfMinLiveTickSampleRatio = 0.0,
    [int]$SoakIterationsPerSample = 10,
    [double]$SoakMinSuccessRatio = 1.0,
    [double]$SoakMinPerSampleSuccessRatio = 1.0,
    [switch]$EnableOnboardingKpiCalibration,
    [switch]$RequireUnityXav2ForWpfOnly,
    [switch]$RequireUnityXav2ForFull = $true,
    [switch]$RequireOnboardingKpiForWpfOnly,
    [switch]$RequireOnboardingKpiForFull = $true,
    [double]$OnboardingWithin3MinSuccessRateThresholdPct = 70.0,
    [int]$OnboardingMinSessionCount = 5,
    [string]$OnboardingTelemetryPath = ".\build\reports\telemetry_latest.json",
    [switch]$SkipOnboardingKpiSummary,
    [switch]$SkipVersionContractCheck,
    [switch]$SkipQualityBaseline,
    [switch]$DisableStrictTrackingContract,
    [string]$MediapipePythonExe = "",
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
$strictTrackingContract = -not $DisableStrictTrackingContract
$effectiveEnableMediapipeSanity = $EnableMediapipeSanity -or $strictTrackingContract
$effectiveEnableHostE2E = $EnableHostE2E -or $strictTrackingContract
$effectiveEnableTrackingFuzz = $EnableTrackingFuzz -or $strictTrackingContract
$fatalError = $null

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
            if ($EnableSpout2Strict) { $args += "-EnableSpout2Strict" }
            if ($RequireSpout2StrictContract) { $args += "-RequireSpout2StrictContract" }
            if ($EnableUnityXav2LtsGate) { $args += "-EnableUnityXav2LtsGate" }
            $args += @("-RenderPerfProfile", $RenderPerfProfile)
            $args += @("-RenderPerfMinLiveTickSampleRatio", "$RenderPerfMinLiveTickSampleRatio")
            $args += @("-SoakIterationsPerSample", "$SoakIterationsPerSample")
            $args += @("-SoakMinSuccessRatio", "$SoakMinSuccessRatio")
            $args += @("-SoakMinPerSampleSuccessRatio", "$SoakMinPerSampleSuccessRatio")
            if ($RenderPerfTargetFps -gt 0) {
                $args += @("-RenderPerfTargetFps", "$RenderPerfTargetFps")
            }
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

    if (-not $SkipOnboardingKpiSummary) {
        $resolvedTelemetryPath = Resolve-AbsolutePath -Path $OnboardingTelemetryPath -BaseDirectory $repoRoot
        if (Test-Path -LiteralPath $resolvedTelemetryPath) {
            $results.Add((Invoke-Step -Name "Onboarding KPI summary" -Action {
                & powershell -ExecutionPolicy Bypass -File .\tools\onboarding_kpi_summary.ps1 -TelemetryPath $resolvedTelemetryPath
            }))
        } else {
            Write-Host "[release-readiness] SKIP: Onboarding KPI summary (telemetry missing: $resolvedTelemetryPath)"
        }
    } else {
        Write-Host "[release-readiness] SKIP: Onboarding KPI summary"
    }

    $results.Add((Invoke-Step -Name "Release gate dashboard refresh" -Action {
        $args = @(
            "-ExecutionPolicy", "Bypass",
            "-File", ".\tools\release_gate_dashboard.ps1"
        )
        if ($RequireUnityXav2ForWpfOnly) { $args += "-RequireUnityXav2ForWpfOnly" }
        if ($RequireUnityXav2ForFull) { $args += "-RequireUnityXav2ForFull" }
        if ($RequireOnboardingKpiForWpfOnly) { $args += "-RequireOnboardingKpiForWpfOnly" }
        if ($RequireOnboardingKpiForFull) { $args += "-RequireOnboardingKpiForFull" }
        $args += @("-OnboardingWithin3MinSuccessRateThresholdPct", "$OnboardingWithin3MinSuccessRateThresholdPct")
        $args += @("-OnboardingMinSessionCount", "$OnboardingMinSessionCount")
        & powershell @args
    }))

    if ($EnableNuGetMirrorBootstrap) {
        $results.Add((Invoke-Step -Name "NuGet mirror bootstrap" -Action {
            & powershell -ExecutionPolicy Bypass -File .\tools\nuget_mirror_bootstrap.ps1
        }))
    }

    if ($EnableUnityXav2EnvBootstrap) {
        $results.Add((Invoke-Step -Name "Unity XAV2 env bootstrap" -Action {
            & powershell -ExecutionPolicy Bypass -File .\tools\unity_xav2_env_bootstrap.ps1
        }))
    }

    if ($EnableXav2CorpusPrep) {
        $results.Add((Invoke-Step -Name "XAV2 gate corpus prepare" -Action {
            $args = @(
                "-ExecutionPolicy", "Bypass",
                "-File", ".\tools\xav2_prepare_gate_corpus.ps1",
                "-SourceDir", $Xav2CorpusSourceDir,
                "-OutputDir", $Xav2CorpusOutputDir,
                "-MinSampleCount", "$Xav2CorpusMinSampleCount"
            )
            if ($Xav2CorpusIncludeBuildArtifacts) { $args += "-IncludeBuildArtifacts" }
            & powershell @args
        }))
    }

    if ($EnableOnboardingKpiCalibration) {
        $results.Add((Invoke-Step -Name "Onboarding KPI calibration" -Action {
            $args = @(
                "-ExecutionPolicy", "Bypass",
                "-File", ".\tools\onboarding_kpi_calibrate.ps1",
                "-TelemetryPath", $OnboardingTelemetryPath,
                "-FloorThresholdPct", "$OnboardingWithin3MinSuccessRateThresholdPct",
                "-MinSessionCountFloor", "$OnboardingMinSessionCount"
            )
            & powershell @args
        }))
    }

    if ($effectiveEnableMediapipeSanity) {
        $results.Add((Invoke-Step -Name "MediaPipe sidecar sanity" -Action {
            $args = @(
                "-ExecutionPolicy", "Bypass",
                "-File", ".\tools\mediapipe_sidecar_sanity.ps1",
                "-RequireExplicitPythonExe"
            )
            if (-not [string]::IsNullOrWhiteSpace($MediapipePythonExe)) {
                $args += @("-PythonExe", $MediapipePythonExe)
            }
            & powershell @args
        }))
    }

    if ($effectiveEnableHostE2E) {
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

    if ($EnableHostOnboardingStateSmoke) {
        $results.Add((Invoke-Step -Name "Host onboarding state smoke" -Action {
            & powershell -ExecutionPolicy Bypass -File .\tools\host_onboarding_state_smoke.ps1
        }))
    }

    if ($effectiveEnableTrackingFuzz) {
        $results.Add((Invoke-Step -Name "Tracking parser fuzz gate" -Action {
            $args = @(
                "-ExecutionPolicy", "Bypass",
                "-File", ".\tools\tracking_parser_fuzz_gate.ps1"
            )
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

    if ($EnableWinUiBlockerTriage -and $IncludeWinUi) {
        $results.Add((Invoke-Step -Name "WinUI blocker triage" -Action {
            $args = @(
                "-ExecutionPolicy", "Bypass",
                "-File", ".\tools\winui_blocker_triage.ps1",
                "-Configuration", $Configuration
            )
            if ($NoRestore) { $args += "-NoRestore" }
            & powershell @args
        }))
    }
}
catch {
    $fatalError = $_
}
finally {
    Pop-Location
}

$dashboardJsonPath = Join-Path $repoRoot "build\reports\release_gate_dashboard.json"
$dashboardReleaseCandidateWpfOnly = "UNKNOWN"
$dashboardReleaseCandidateFull = "UNKNOWN"
$dashboardTrackingContract = "UNKNOWN"
if (Test-Path -LiteralPath $dashboardJsonPath) {
    try {
        $dashboard = Get-Content -Raw -Path $dashboardJsonPath | ConvertFrom-Json
        if ($null -ne $dashboard.gate_summary) {
            $dashboardReleaseCandidateWpfOnly = if ([bool]$dashboard.gate_summary.release_candidate_wpf_only) { "PASS" } else { "FAIL" }
            $dashboardReleaseCandidateFull = if ([bool]$dashboard.gate_summary.release_candidate_full) { "PASS" } else { "FAIL" }
            $dashboardTrackingContract = if ([bool]$dashboard.gate_summary.tracking_contract_all_pass) { "PASS" } else { "FAIL" }
        }
    } catch {
        $dashboardReleaseCandidateWpfOnly = "INVALID_JSON"
        $dashboardReleaseCandidateFull = "INVALID_JSON"
        $dashboardTrackingContract = "INVALID_JSON"
    }
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
$lines.Add("EnableTrackingFuzz: $EnableTrackingFuzz")
$lines.Add("EnableHostOnboardingStateSmoke: $EnableHostOnboardingStateSmoke")
$lines.Add("EnableWinUiMinRepro: $EnableWinUiMinRepro")
$lines.Add("EnableWinUiBlockerTriage: $EnableWinUiBlockerTriage")
$lines.Add("EnableNuGetMirrorBootstrap: $EnableNuGetMirrorBootstrap")
$lines.Add("EnableMediapipeSanity: $EnableMediapipeSanity")
$lines.Add("MediapipePythonExe: $(if ([string]::IsNullOrWhiteSpace($MediapipePythonExe)) { '<env:VSFCLONE_MEDIAPIPE_PYTHON>' } else { $MediapipePythonExe })")
$lines.Add("StrictTrackingContract: $strictTrackingContract")
$lines.Add("EffectiveEnableHostE2E: $effectiveEnableHostE2E")
$lines.Add("EffectiveEnableTrackingFuzz: $effectiveEnableTrackingFuzz")
$lines.Add("EffectiveEnableMediapipeSanity: $effectiveEnableMediapipeSanity")
$lines.Add("EnableUnityXav2EnvBootstrap: $EnableUnityXav2EnvBootstrap")
$lines.Add("EnableXav2CorpusPrep: $EnableXav2CorpusPrep")
$lines.Add("Xav2CorpusSourceDir: $Xav2CorpusSourceDir")
$lines.Add("Xav2CorpusOutputDir: $Xav2CorpusOutputDir")
$lines.Add("Xav2CorpusMinSampleCount: $Xav2CorpusMinSampleCount")
$lines.Add("Xav2CorpusIncludeBuildArtifacts: $Xav2CorpusIncludeBuildArtifacts")
$lines.Add("EnableSpout2Interop: $EnableSpout2Interop")
$lines.Add("EnableSpout2Strict: $EnableSpout2Strict")
$lines.Add("RequireSpout2StrictContract: $RequireSpout2StrictContract")
$lines.Add("EnableUnityXav2LtsGate: $EnableUnityXav2LtsGate")
$lines.Add("EnableXav2CompressionQuality: $EnableXav2CompressionQuality")
$lines.Add("EnableXav2Parity: $EnableXav2Parity")
$lines.Add("RenderPerfProfile: $RenderPerfProfile")
$lines.Add("RenderPerfTargetFps: $RenderPerfTargetFps")
$lines.Add("RenderPerfMinLiveTickSampleRatio: $RenderPerfMinLiveTickSampleRatio")
$lines.Add("SoakIterationsPerSample: $SoakIterationsPerSample")
$lines.Add("SoakMinSuccessRatio: $SoakMinSuccessRatio")
$lines.Add("SoakMinPerSampleSuccessRatio: $SoakMinPerSampleSuccessRatio")
$lines.Add("EnableOnboardingKpiCalibration: $EnableOnboardingKpiCalibration")
$lines.Add("RequireUnityXav2ForWpfOnly: $RequireUnityXav2ForWpfOnly")
$lines.Add("RequireUnityXav2ForFull: $RequireUnityXav2ForFull")
$lines.Add("RequireOnboardingKpiForWpfOnly: $RequireOnboardingKpiForWpfOnly")
$lines.Add("RequireOnboardingKpiForFull: $RequireOnboardingKpiForFull")
$lines.Add("OnboardingWithin3MinSuccessRateThresholdPct: $OnboardingWithin3MinSuccessRateThresholdPct")
$lines.Add("OnboardingMinSessionCount: $OnboardingMinSessionCount")
$lines.Add("OnboardingTelemetryPath: $OnboardingTelemetryPath")
$lines.Add("SkipOnboardingKpiSummary: $SkipOnboardingKpiSummary")
$lines.Add("DashboardJsonPath: $dashboardJsonPath")
$lines.Add("DashboardTrackingContract: $dashboardTrackingContract")
$lines.Add("DashboardReleaseCandidateWpfOnly: $dashboardReleaseCandidateWpfOnly")
$lines.Add("DashboardReleaseCandidateFull: $dashboardReleaseCandidateFull")
$lines.Add("DurationSec: $durationSec")
if ($null -ne $fatalError) {
    $lines.Add("FatalError: $($fatalError.Exception.Message)")
}
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
$lines.Add("- build/reports/onboarding_kpi_summary.txt")
$lines.Add("- build/reports/onboarding_kpi_summary.json")
$lines.Add("- build/reports/host_e2e_gate_summary.txt")
$lines.Add("- build/reports/tracking_parser_fuzz_gate_summary.txt")
$lines.Add("- build/reports/winui_xaml_min_repro_summary.txt")
$lines.Add("- build/reports/winui_xaml_min_repro_summary.json")
$lines.Add("- build/reports/winui_blocker_triage_summary.txt")
$lines.Add("- build/reports/winui_blocker_triage_summary.json")
$lines.Add("- build/reports/nuget_mirror_bootstrap_summary.txt")
$lines.Add("- build/reports/unity_xav2_env_bootstrap_summary.txt")
$lines.Add("- build/reports/mediapipe_sidecar_sanity_summary.txt")
$lines.Add("- build/reports/host_onboarding_state_smoke_summary.txt")
$lines.Add("- build/reports/onboarding_kpi_calibration.txt")
$lines.Add("- build/reports/onboarding_kpi_calibration.json")
$lines.Add("- build/gate_corpus/xav2/prepare_summary.txt")
$lines.Add("- build/gate_corpus/xav2/sample_manifest.json")
$lines.Add("- build/reports/spout2_interop_gate_summary.txt")
$lines.Add("- build/reports/unity_xav2_lts_gate_summary.txt")
$lines.Add("- build/reports/unity_xav2_lts_gate_history.csv")
$lines.Add("- build/reports/unity_xav2_lts_kpi_summary.txt")
$lines.Add("- build/reports/xav2_compression_quality_gate_summary.txt")
$lines.Add("- build/reports/xav2_parity_gate_summary.txt")

$lines | Set-Content -Path $resolvedSummaryPath -Encoding UTF8
Write-Host "[release-readiness] Summary: $resolvedSummaryPath"

if ($null -ne $fatalError) {
    Write-Error "[release-readiness] FAILED: $($fatalError.Exception.Message)"
    exit 1
}
