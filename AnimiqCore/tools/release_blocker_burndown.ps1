param(
    [string]$DashboardJsonPath = ".\build\reports\release_gate_dashboard.json",
    [string]$WinUiSummaryPath = ".\build\reports\winui_xaml_min_repro_summary.txt",
    [string]$UnityLtsGateSummaryPath = ".\build\reports\unity_miq_lts_gate_summary.txt",
    [string]$OnboardingKpiSummaryPath = ".\build\reports\onboarding_kpi_summary.txt",
    [string]$OutputMd = ".\build\reports\release_blocker_burndown.md"
)

$ErrorActionPreference = "Stop"

function Resolve-AbsolutePath {
    param([string]$Path, [string]$BaseDirectory)
    if ([System.IO.Path]::IsPathRooted($Path)) {
        return [System.IO.Path]::GetFullPath($Path)
    }
    return [System.IO.Path]::GetFullPath((Join-Path $BaseDirectory $Path))
}

function Read-Line {
    param([string]$Path, [string]$Prefix)
    if (-not (Test-Path -LiteralPath $Path)) { return "MISSING" }
    $line = Get-Content -Path $Path | Where-Object { $_.StartsWith($Prefix) } | Select-Object -First 1
    if ($null -eq $line) { return "UNKNOWN" }
    return $line.Substring($Prefix.Length).Trim()
}

$repoRoot = Split-Path -Parent $PSScriptRoot
$resolvedDashboard = Resolve-AbsolutePath -Path $DashboardJsonPath -BaseDirectory $repoRoot
$resolvedWinUi = Resolve-AbsolutePath -Path $WinUiSummaryPath -BaseDirectory $repoRoot
$resolvedUnity = Resolve-AbsolutePath -Path $UnityLtsGateSummaryPath -BaseDirectory $repoRoot
$resolvedKpi = Resolve-AbsolutePath -Path $OnboardingKpiSummaryPath -BaseDirectory $repoRoot
$resolvedMd = Resolve-AbsolutePath -Path $OutputMd -BaseDirectory $repoRoot
New-Item -ItemType Directory -Force -Path (Split-Path -Parent $resolvedMd) | Out-Null

$wpfCandidate = "UNKNOWN"
$fullCandidate = "UNKNOWN"
if (Test-Path -LiteralPath $resolvedDashboard) {
    try {
        $dash = Get-Content -Raw -Path $resolvedDashboard | ConvertFrom-Json
        if ($null -ne $dash.gate_summary) {
            $wpfCandidate = if ([bool]$dash.gate_summary.release_candidate_wpf_only) { "PASS" } else { "FAIL" }
            $fullCandidate = if ([bool]$dash.gate_summary.release_candidate_full) { "PASS" } else { "FAIL" }
        }
    } catch {
        $wpfCandidate = "INVALID_JSON"
        $fullCandidate = "INVALID_JSON"
    }
}

$winuiFailureClass = Read-Line -Path $resolvedWinUi -Prefix "FailureClass:"
$unityOverall = Read-Line -Path $resolvedUnity -Prefix "Overall:"
$onboardingSessions = Read-Line -Path $resolvedKpi -Prefix "SessionCount:"
$onboardingRate = Read-Line -Path $resolvedKpi -Prefix "Within3MinSuccessRatePct:"

$md = [System.Collections.Generic.List[string]]::new()
$md.Add("# Release Blocker Burndown")
$md.Add("")
$md.Add("Generated: $((Get-Date).ToUniversalTime().ToString('o'))")
$md.Add("")
$md.Add("| Blocker | Current | Owner | Target Date | Notes |")
$md.Add("|---|---|---|---|---|")
$md.Add("| WinUI toolchain (WMC9999 lane) | $winuiFailureClass | _unassigned_ | _tbd_ | Source: winui_xaml_min_repro summary |")
$md.Add("| Unity MIQ full-chain | $unityOverall | _unassigned_ | _tbd_ | Source: unity_miq_lts_gate summary |")
$md.Add("| Onboarding KPI sufficiency | sessions=$onboardingSessions, rate=$onboardingRate | _unassigned_ | _tbd_ | Policy default: min sessions >= 5 |")
$md.Add("")
$md.Add("## Candidate Status")
$md.Add("- ReleaseCandidateWpfOnly: $wpfCandidate")
$md.Add("- ReleaseCandidateFull: $fullCandidate")
$md.Add("")
$md.Add("## Next Weekly Actions")
$md.Add("1. Assign owner + due date for each blocker row.")
$md.Add("2. Attach latest artifact links next to each blocker.")
$md.Add("3. Keep this file updated during weekly release review.")
$md | Set-Content -Path $resolvedMd -Encoding UTF8

Write-Host "markdown=$resolvedMd"

