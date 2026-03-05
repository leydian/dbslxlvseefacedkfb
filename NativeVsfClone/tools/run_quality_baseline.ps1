param(
    [switch]$SkipVsfAvatar,
    [switch]$SkipVsfRender,
    [switch]$SkipVrm,
    [switch]$SkipVxAvatar,
    [switch]$EnableVsfTrend,
    [switch]$EnableRenderPerf,
    [switch]$EnableSoak,
    [switch]$EnableSessionMigration,
    [switch]$EnableTrackingFuzz,
    [switch]$EnableSampleProfileResolve,
    [switch]$EnableHostE2E,
    [switch]$EnableNuGetMirrorBootstrap,
    [string]$SummaryPath = ".\build\reports\quality_baseline_summary.txt"
)

$ErrorActionPreference = "Stop"

function Invoke-Gate {
    param(
        [Parameter(Mandatory = $true)][string]$Name,
        [Parameter(Mandatory = $true)][string]$Command
    )

    Write-Host "[quality-baseline] Running $Name..."
    Invoke-Expression $Command
    $exitCode = $LASTEXITCODE
    if ($exitCode -eq 0) {
        Write-Host "[quality-baseline] ${Name}: PASS"
    } else {
        Write-Host "[quality-baseline] ${Name}: FAIL (exit=$exitCode)"
    }

    return [PSCustomObject]@{
        Name = $Name
        ExitCode = $exitCode
        Status = if ($exitCode -eq 0) { "PASS" } else { "FAIL" }
        Command = $Command
    }
}

$repoRoot = Split-Path -Parent $PSScriptRoot
$resolvedSummaryPath = if ([System.IO.Path]::IsPathRooted($SummaryPath)) {
    [System.IO.Path]::GetFullPath($SummaryPath)
} else {
    [System.IO.Path]::GetFullPath((Join-Path $repoRoot $SummaryPath))
}
$summaryDir = Split-Path -Parent $resolvedSummaryPath
New-Item -ItemType Directory -Force -Path $summaryDir | Out-Null

$results = [System.Collections.Generic.List[object]]::new()

if (-not $SkipVsfAvatar) {
    $results.Add((Invoke-Gate `
        -Name "VSFAvatar fixed-set gate" `
        -Command "powershell -ExecutionPolicy Bypass -File .\tools\vsfavatar_quality_gate.ps1 -UseFixedSet"))
}

if (-not $SkipVsfRender) {
    $results.Add((Invoke-Gate `
        -Name "VSFAvatar render gate" `
        -Command "powershell -ExecutionPolicy Bypass -File .\tools\vsfavatar_render_gate.ps1 -UseFixedSet"))
}

if (-not $SkipVrm) {
    $results.Add((Invoke-Gate `
        -Name "VRM fixed5 gate" `
        -Command "powershell -ExecutionPolicy Bypass -File .\tools\vrm_quality_gate.ps1 -Profile fixed5"))
}

if (-not $SkipVxAvatar) {
    $results.Add((Invoke-Gate `
        -Name "VXAvatar quick fixed-set gate" `
        -Command "powershell -ExecutionPolicy Bypass -File .\tools\vxavatar_quality_gate.ps1 -UseFixedSet -Profile quick"))
}

if ($EnableVsfTrend) {
    $results.Add((Invoke-Gate `
        -Name "VSFAvatar GateD trend summary" `
        -Command "powershell -ExecutionPolicy Bypass -File .\tools\vsfavatar_gated_trend.ps1"))
}

if ($EnableRenderPerf) {
    $results.Add((Invoke-Gate `
        -Name "Render performance numeric gate" `
        -Command "powershell -ExecutionPolicy Bypass -File .\tools\render_perf_gate.ps1"))
}

if ($EnableSoak) {
    $results.Add((Invoke-Gate `
        -Name "Avatar load soak gate" `
        -Command "powershell -ExecutionPolicy Bypass -File .\tools\avatar_load_soak_gate.ps1"))
}

if ($EnableSessionMigration) {
    $results.Add((Invoke-Gate `
        -Name "Session state migration check" `
        -Command "powershell -ExecutionPolicy Bypass -File .\tools\session_state_migration_check.ps1"))
}

if ($EnableTrackingFuzz) {
    $results.Add((Invoke-Gate `
        -Name "Tracking parser fuzz gate" `
        -Command "powershell -ExecutionPolicy Bypass -File .\tools\tracking_parser_fuzz_gate.ps1"))
}

if ($EnableSampleProfileResolve) {
    $results.Add((Invoke-Gate `
        -Name "Sample profile resolve (fixed_set)" `
        -Command "powershell -ExecutionPolicy Bypass -File .\tools\sample_profile_resolve.ps1 -Profile fixed_set"))
}

if ($EnableNuGetMirrorBootstrap) {
    $results.Add((Invoke-Gate `
        -Name "NuGet mirror bootstrap" `
        -Command "powershell -ExecutionPolicy Bypass -File .\tools\nuget_mirror_bootstrap.ps1"))
}

if ($EnableHostE2E) {
    $results.Add((Invoke-Gate `
        -Name "Host E2E gate" `
        -Command "powershell -ExecutionPolicy Bypass -File .\tools\host_e2e_gate.ps1 -SkipNativeBuild -NoRestore"))
}

$overallStatus = if ($results | Where-Object { $_.ExitCode -ne 0 }) { "FAIL" } else { "PASS" }
$lines = [System.Collections.Generic.List[string]]::new()
$lines.Add("Quality Baseline Summary")
$lines.Add("Generated: $(Get-Date -Format s)")
$lines.Add("Overall: $overallStatus")
$lines.Add("")
$lines.Add("Results:")
foreach ($result in $results) {
    $lines.Add("- $($result.Name): $($result.Status) (exit=$($result.ExitCode))")
    $lines.Add("  command: $($result.Command)")
}
$lines.Add("")
$lines.Add("Artifacts:")
$lines.Add("- VSFAvatar: build/reports/vsfavatar_gate_summary.txt")
$lines.Add("- VSFAvatarRender: build/reports/vsfavatar_render_gate_summary.txt")
$lines.Add("- VRM: build/reports/vrm_gate_fixed5.txt")
$lines.Add("- VXAvatar: build/reports/vxavatar_gate_summary.txt")
$lines.Add("- VSFAvatarTrend: build/reports/vsfavatar_gate_trend_latest.txt")
$lines.Add("- RenderPerf: build/reports/render_perf_gate_summary.txt")
$lines.Add("- AvatarSoak: build/reports/avatar_load_soak_gate_summary.txt")
$lines.Add("- SessionMigration: build/reports/session_state_migration_check_summary.txt")
$lines.Add("- TrackingFuzz: build/reports/tracking_parser_fuzz_gate_summary.txt")
$lines.Add("- SampleProfileResolve: build/reports/sample_profile_resolve_latest.txt")
$lines.Add("- NuGetMirrorBootstrap: build/reports/nuget_mirror_bootstrap_summary.txt")
$lines.Add("- HostE2E: build/reports/host_e2e_gate_summary.txt")

$lines | Set-Content -Path $resolvedSummaryPath -Encoding UTF8
Write-Host "[quality-baseline] Summary: $resolvedSummaryPath"

if ($overallStatus -eq "FAIL") {
    exit 1
}
