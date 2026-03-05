param(
    [Parameter(Mandatory = $true)][string]$ExePath,
    [string]$WorkingDirectory = "",
    [int]$AliveSeconds = 6,
    [string]$ReportPath = ".\build\reports\wpf_launch_smoke_latest.txt",
    [bool]$TreatFailureAsError = $false
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
$resolvedExePath = Resolve-AbsolutePath -Path $ExePath -BaseDirectory $repoRoot
$resolvedWorkingDirectory = if ([string]::IsNullOrWhiteSpace($WorkingDirectory)) {
    Split-Path -Parent $resolvedExePath
} else {
    Resolve-AbsolutePath -Path $WorkingDirectory -BaseDirectory $repoRoot
}
$resolvedReportPath = Resolve-AbsolutePath -Path $ReportPath -BaseDirectory $repoRoot
$reportDir = Split-Path -Parent $resolvedReportPath
New-Item -ItemType Directory -Force -Path $reportDir | Out-Null

$lines = [System.Collections.Generic.List[string]]::new()
$lines.Add("WPF launch smoke run: $(Get-Date -Format o)")
$lines.Add("ExePath: $resolvedExePath")
$lines.Add("WorkingDirectory: $resolvedWorkingDirectory")
$lines.Add("AliveSeconds: $AliveSeconds")

if (-not (Test-Path $resolvedExePath)) {
    $errorText = "WPF smoke target not found: $resolvedExePath"
    $lines.Add("Status: FAIL")
    $lines.Add("Error: $errorText")
    $lines | Set-Content -Path $resolvedReportPath -Encoding UTF8
    throw $errorText
}

$status = "PASS"
$exitCode = 0
$process = $null

try {
    $process = Start-Process -FilePath $resolvedExePath -WorkingDirectory $resolvedWorkingDirectory -PassThru
    Start-Sleep -Seconds $AliveSeconds

    if ($process.HasExited) {
        $status = "FAIL"
        $exitCode = $process.ExitCode
    }
} catch {
    $status = "FAIL"
    $exitCode = -1
    $lines.Add("Exception: $($_.Exception.Message)")
} finally {
    if ($null -ne $process -and -not $process.HasExited) {
        try {
            Stop-Process -Id $process.Id -Force -ErrorAction SilentlyContinue
        } catch {
            # ignore
        }
    }
}

$lines.Add("Status: $status")
$lines.Add("ExitCode: $exitCode")

$startTime = (Get-Date).AddMinutes(-20)
try {
    $events = Get-WinEvent -FilterHashtable @{
        LogName = "Application"
        ProviderName = ".NET Runtime"
        Id = 1026
        StartTime = $startTime
    } -ErrorAction SilentlyContinue | Select-Object -First 3
    if ($null -ne $events -and @($events).Count -gt 0) {
        $lines.Add("EventLog: .NET Runtime 1026 entries (latest up to 3)")
        foreach ($event in $events) {
            $msg = "$($event.Message)"
            $msg = $msg -replace "`r", " " -replace "`n", " "
            if ($msg.Length -gt 500) {
                $msg = $msg.Substring(0, 500)
            }
            $lines.Add(" - [$($event.TimeCreated.ToString("o"))] $msg")
        }
    } else {
        $lines.Add("EventLog: no .NET Runtime 1026 entries found in last 20 minutes.")
    }
} catch {
    $lines.Add("EventLog: query failed ($($_.Exception.Message))")
}

$lines | Set-Content -Path $resolvedReportPath -Encoding UTF8

if ($status -ne "PASS" -and $TreatFailureAsError) {
    throw "WPF launch smoke failed (ExitCode=$exitCode). See report: $resolvedReportPath"
}

return [ordered]@{
    status = $status
    exit_code = $exitCode
    report_path = $resolvedReportPath
}
