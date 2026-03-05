param(
    [Parameter(Mandatory = $true)][string]$ExePath,
    [string]$WorkingDirectory = "",
    [int]$AliveSeconds = 6,
    [string]$ReportPath = ".\build\reports\wpf_launch_smoke_latest.txt",
    [bool]$TreatFailureAsError = $false,
    [string[]]$AdditionalProbePaths = @(),
    [bool]$IncludeDirectoryInventory = $true
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

$probePaths = [System.Collections.Generic.List[string]]::new()
$probePaths.Add($resolvedWorkingDirectory)
foreach ($candidate in $AdditionalProbePaths) {
    if (-not [string]::IsNullOrWhiteSpace($candidate)) {
        $resolvedCandidate = Resolve-AbsolutePath -Path $candidate -BaseDirectory $repoRoot
        if (-not $probePaths.Contains($resolvedCandidate)) {
            $probePaths.Add($resolvedCandidate)
        }
    }
}

$runStart = Get-Date
$lines = [System.Collections.Generic.List[string]]::new()
$lines.Add("WPF launch smoke run: $(Get-Date -Format o)")
$lines.Add("ExePath: $resolvedExePath")
$lines.Add("WorkingDirectory: $resolvedWorkingDirectory")
$lines.Add("AliveSeconds: $AliveSeconds")
$lines.Add("ProbePaths: $($probePaths -join ';')")

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
$originalPath = $env:PATH

try {
    $env:PATH = (($probePaths | Where-Object { Test-Path $_ }) -join ";") + ";" + $env:PATH
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
    $env:PATH = $originalPath
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

$startTime = $runStart.AddSeconds(-2)
try {
    $events = Get-WinEvent -FilterHashtable @{
        LogName = "Application"
        StartTime = $startTime
    } -ErrorAction SilentlyContinue | Where-Object {
        ($_.Id -in @(1026, 1000, 1001)) -and ($_.Message -match "WpfHost\.exe|DllNotFoundException")
    } | Select-Object -First 5

    $eventMessages = [System.Collections.Generic.List[string]]::new()
    if ($null -ne $events -and @($events).Count -gt 0) {
        $lines.Add("EventLog: related Application entries (IDs 1026/1000/1001, latest up to 5)")
        $lines.Add("EventLogStartTime: $($startTime.ToString("o"))")
        foreach ($event in $events) {
            $msg = "$($event.Message)"
            $msg = $msg -replace "`r", " " -replace "`n", " "
            if ($msg.Length -gt 500) {
                $msg = $msg.Substring(0, 500)
            }
            $eventMessages.Add($msg)
            $lines.Add(" - [$($event.TimeCreated.ToString("o"))] id=$($event.Id) provider=$($event.ProviderName) $msg")
        }

        $dependencyHints = [System.Collections.Generic.HashSet[string]]::new()
        foreach ($message in $eventMessages) {
            if ($message -match "Could not load file or assembly '([^']+)'") {
                [void]$dependencyHints.Add($Matches[1])
            }
            if ($message -match "Unable to load DLL '([^']+)'") {
                [void]$dependencyHints.Add($Matches[1])
            }
            if ($message -match "Faulting module name:\s*([^,\s]+)") {
                [void]$dependencyHints.Add($Matches[1])
            }
        }
        if ($dependencyHints.Count -gt 0) {
            $lines.Add("DependencyHints: " + ((@($dependencyHints) | Sort-Object) -join ", "))
        } else {
            $lines.Add("DependencyHints: none extracted from event messages.")
        }
    } else {
        $lines.Add("EventLog: no related entries found since smoke run start.")
        $lines.Add("DependencyHints: unavailable (no matching event log records).")
    }
} catch {
    $lines.Add("EventLog: query failed ($($_.Exception.Message))")
    $lines.Add("DependencyHints: unavailable (event log query failed).")
}

if ($IncludeDirectoryInventory) {
    $lines.Add("DirectoryInventory:")
    foreach ($dirPath in $probePaths) {
        if (-not (Test-Path $dirPath)) {
            $lines.Add(" - $dirPath (missing)")
            continue
        }
        $lines.Add(" - $dirPath")
        $dllFiles = @(Get-ChildItem -Path $dirPath -Filter *.dll -File -ErrorAction SilentlyContinue | Select-Object -First 20)
        if ($dllFiles.Count -eq 0) {
            $lines.Add("   dlls: <none>")
            continue
        }
        foreach ($dllFile in $dllFiles) {
            $lines.Add("   dll: $($dllFile.Name)")
        }
        if ((Get-ChildItem -Path $dirPath -Filter *.dll -File -ErrorAction SilentlyContinue).Count -gt 20) {
            $lines.Add("   dll: <truncated>")
        }
    }
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
