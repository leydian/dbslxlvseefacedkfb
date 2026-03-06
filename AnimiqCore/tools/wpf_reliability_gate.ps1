param(
    [int]$Iterations = 3,
    [string]$Configuration = "Release",
    [string]$RuntimeIdentifier = "win-x64",
    [switch]$SkipNativeBuild,
    [switch]$RunQualityBaselineAtEnd,
    [bool]$StopOnFailure = $true,
    [string]$ReportPath = ".\build\reports\wpf_reliability_gate_latest.txt",
    [string]$HostPublishReportPath = ".\build\reports\host_publish_latest.txt",
    [string]$WpfSmokeReportPath = ".\build\reports\wpf_launch_smoke_latest.txt"
)

$ErrorActionPreference = "Stop"

function Resolve-AbsolutePath {
    param([string]$Path, [string]$BaseDirectory)
    if ([System.IO.Path]::IsPathRooted($Path)) {
        return [System.IO.Path]::GetFullPath($Path)
    }
    return [System.IO.Path]::GetFullPath((Join-Path $BaseDirectory $Path))
}

function Get-ReportValue {
    param([string[]]$Lines, [string]$Prefix)
    foreach ($line in $Lines) {
        if ($line.StartsWith($Prefix)) {
            return $line.Substring($Prefix.Length).Trim()
        }
    }
    return ""
}

$repoRoot = Split-Path -Parent $PSScriptRoot
$resolvedReportPath = Resolve-AbsolutePath -Path $ReportPath -BaseDirectory $repoRoot
$resolvedHostPublishReportPath = Resolve-AbsolutePath -Path $HostPublishReportPath -BaseDirectory $repoRoot
$resolvedWpfSmokeReportPath = Resolve-AbsolutePath -Path $WpfSmokeReportPath -BaseDirectory $repoRoot
New-Item -ItemType Directory -Force -Path (Split-Path -Parent $resolvedReportPath) | Out-Null

$lines = [System.Collections.Generic.List[string]]::new()
$lines.Add("WPF reliability gate run: $(Get-Date -Format o)")
$lines.Add("Iterations: $Iterations")
$lines.Add("Configuration: $Configuration")
$lines.Add("RuntimeIdentifier: $RuntimeIdentifier")
$lines.Add("SkipNativeBuild: $SkipNativeBuild")
$lines.Add("RunQualityBaselineAtEnd: $RunQualityBaselineAtEnd")
$lines.Add("StopOnFailure: $StopOnFailure")
$lines.Add("HostPublishReportPath: $resolvedHostPublishReportPath")
$lines.Add("WpfSmokeReportPath: $resolvedWpfSmokeReportPath")

$passCount = 0
$failCount = 0
$runStart = Get-Date

for ($i = 1; $i -le $Iterations; $i++) {
    $iterationStart = Get-Date
    $lines.Add("Iteration[$i]: START $(Get-Date -Format o)")

    $publishScriptPath = Join-Path $repoRoot "tools\publish_hosts.ps1"
    if ($SkipNativeBuild) {
        & $publishScriptPath `
            -Configuration $Configuration `
            -RuntimeIdentifier $RuntimeIdentifier `
            -RunWpfLaunchSmoke $true `
            -WpfLaunchSmokeFailOnError $true `
            -SkipNativeBuild | Out-Host
    } else {
        & $publishScriptPath `
            -Configuration $Configuration `
            -RuntimeIdentifier $RuntimeIdentifier `
            -RunWpfLaunchSmoke $true `
            -WpfLaunchSmokeFailOnError $true | Out-Host
    }
    $publishExitCode = $LASTEXITCODE
    $lines.Add("Iteration[$i]: publish_hosts exit=$publishExitCode")
    if ($publishExitCode -ne 0) {
        $failCount++
        $lines.Add("Iteration[$i]: FAIL (publish_hosts)")
        if ($StopOnFailure) {
            $lines.Add("Iteration[$i]: stop-on-failure triggered")
            break
        }
        continue
    }

    $hostLines = if (Test-Path $resolvedHostPublishReportPath) {
        Get-Content -Path $resolvedHostPublishReportPath -ErrorAction SilentlyContinue
    } else {
        @()
    }
    $smokeLines = if (Test-Path $resolvedWpfSmokeReportPath) {
        Get-Content -Path $resolvedWpfSmokeReportPath -ErrorAction SilentlyContinue
    } else {
        @()
    }

    $smokeStatus = Get-ReportValue -Lines $smokeLines -Prefix "Status:"
    $smokeExitCodeText = Get-ReportValue -Lines $smokeLines -Prefix "ExitCode:"
    $hostMode = Get-ReportValue -Lines $hostLines -Prefix "HostPublishMode:"
    $lines.Add("Iteration[$i]: host mode=$hostMode")
    $lines.Add("Iteration[$i]: smoke status=$smokeStatus exit=$smokeExitCodeText")

    if ($smokeStatus -ne "PASS") {
        $failCount++
        $lines.Add("Iteration[$i]: FAIL (wpf smoke)")
        if ($StopOnFailure) {
            $lines.Add("Iteration[$i]: stop-on-failure triggered")
            break
        }
    } else {
        $passCount++
        $lines.Add("Iteration[$i]: PASS")
    }

    $iterationDuration = [int]((Get-Date) - $iterationStart).TotalSeconds
    $lines.Add("Iteration[$i]: duration_sec=$iterationDuration")
}

$baselineExitCode = 0
if ($RunQualityBaselineAtEnd) {
    $lines.Add("QualityBaseline: START $(Get-Date -Format o)")
    & (Join-Path $repoRoot "tools\run_quality_baseline.ps1") | Out-Host
    $baselineExitCode = $LASTEXITCODE
    $lines.Add("QualityBaseline: exit=$baselineExitCode")
}

$totalDuration = [int]((Get-Date) - $runStart).TotalSeconds
$effectiveIterations = $passCount + $failCount
$lines.Add("Summary: pass=$passCount fail=$failCount effective_iterations=$effectiveIterations requested_iterations=$Iterations")
$lines.Add("Summary: total_duration_sec=$totalDuration")

$status = "PASS"
if ($failCount -gt 0 -or $baselineExitCode -ne 0) {
    $status = "FAIL"
}
$lines.Add("Status: $status")

$lines | Set-Content -Path $resolvedReportPath -Encoding UTF8

if ($status -ne "PASS") {
    throw "WPF reliability gate failed. See report: $resolvedReportPath"
}

return [ordered]@{
    status = $status
    pass_count = $passCount
    fail_count = $failCount
    report_path = $resolvedReportPath
}
