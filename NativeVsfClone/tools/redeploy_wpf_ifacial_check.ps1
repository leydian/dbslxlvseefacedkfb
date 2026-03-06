param(
    [string]$Configuration = "Release",
    [string]$RuntimeIdentifier = "win-x64",
    [switch]$NoRestore,
    [switch]$SkipPublish,
    [bool]$AutoLaunch = $true,
    [switch]$NonInteractive,
    [int]$ObserveSeconds = 25,
    [int]$PollIntervalMs = 1000,
    [string]$ReportPath = ".\build\reports\ifacial_redeploy_check_latest.txt"
)

$ErrorActionPreference = "Stop"

function Resolve-AbsolutePath {
    param([string]$Path, [string]$BaseDirectory)
    if ([System.IO.Path]::IsPathRooted($Path)) {
        return [System.IO.Path]::GetFullPath($Path)
    }
    return [System.IO.Path]::GetFullPath((Join-Path $BaseDirectory $Path))
}

function Parse-TrackingCounters {
    param([string]$Line)

    if ([string]::IsNullOrWhiteSpace($Line)) {
        return $null
    }
    if ($Line -notmatch "tracking=") {
        return $null
    }

    $packetsMatch = [regex]::Match($Line, "packets=(\d+)")
    $parseErrMatch = [regex]::Match($Line, "parse_err=(\d+)")
    if (-not $packetsMatch.Success -or -not $parseErrMatch.Success) {
        return $null
    }

    return [ordered]@{
        packets = [ulong]$packetsMatch.Groups[1].Value
        parse_err = [ulong]$parseErrMatch.Groups[1].Value
        line = $Line
    }
}

function Get-WpfTrackingStatusLine {
    param([int]$ProcessId)

    Add-Type -AssemblyName UIAutomationClient
    Add-Type -AssemblyName UIAutomationTypes

    $root = [System.Windows.Automation.AutomationElement]::RootElement
    $pidProp = [System.Windows.Automation.AutomationElement]::ProcessIdProperty
    $pidCondition = New-Object System.Windows.Automation.PropertyCondition($pidProp, $ProcessId)
    $window = $root.FindFirst([System.Windows.Automation.TreeScope]::Children, $pidCondition)
    if ($null -eq $window) {
        return $null
    }

    $elements = $window.FindAll([System.Windows.Automation.TreeScope]::Descendants, [System.Windows.Automation.Condition]::TrueCondition)
    if ($null -eq $elements) {
        return $null
    }

    for ($i = 0; $i -lt $elements.Count; $i++) {
        $element = $elements.Item($i)
        if ($null -eq $element) {
            continue
        }

        try {
            $nameText = $element.Current.Name
            if (-not [string]::IsNullOrWhiteSpace($nameText) -and $nameText.Contains("tracking=") -and $nameText.Contains("packets=") -and $nameText.Contains("parse_err=")) {
                return $nameText
            }
        } catch {
            # Best effort only.
        }

        try {
            $valuePatternObj = $null
            if ($element.TryGetCurrentPattern([System.Windows.Automation.ValuePattern]::Pattern, [ref]$valuePatternObj)) {
                $valueText = ([System.Windows.Automation.ValuePattern]$valuePatternObj).Current.Value
                if (-not [string]::IsNullOrWhiteSpace($valueText) -and $valueText.Contains("tracking=") -and $valueText.Contains("packets=") -and $valueText.Contains("parse_err=")) {
                    $lines = $valueText -split "(`r`n|`n)"
                    foreach ($line in $lines) {
                        if ($line.Contains("tracking=") -and $line.Contains("packets=") -and $line.Contains("parse_err=")) {
                            return $line.Trim()
                        }
                    }
                }
            }
        } catch {
            # Best effort only.
        }
    }

    return $null
}

$repoRoot = Split-Path -Parent $PSScriptRoot
$resolvedReportPath = Resolve-AbsolutePath -Path $ReportPath -BaseDirectory $repoRoot
$reportDir = Split-Path -Parent $resolvedReportPath
$resolvedJsonPath = [System.IO.Path]::ChangeExtension($resolvedReportPath, ".json")
New-Item -ItemType Directory -Force -Path $reportDir | Out-Null

$lines = [System.Collections.Generic.List[string]]::new()
$startedUtc = [DateTimeOffset]::UtcNow
$lines.Add("WPF redeploy + iFacial check run: $($startedUtc.ToString('o'))")
$lines.Add("Configuration: $Configuration")
$lines.Add("RuntimeIdentifier: $RuntimeIdentifier")
$lines.Add("NoRestore: $NoRestore")
$lines.Add("SkipPublish: $SkipPublish")
$lines.Add("AutoLaunch: $AutoLaunch")
$lines.Add("NonInteractive: $($NonInteractive.IsPresent)")
$lines.Add("ObserveSeconds: $ObserveSeconds")
$lines.Add("PollIntervalMs: $PollIntervalMs")
$lines.Add("")

Push-Location $repoRoot
try {
    if (-not $SkipPublish) {
        $lines.Add("Step: publish_hosts.ps1 (WPF only)")
        & (Join-Path $repoRoot "tools\publish_hosts.ps1") `
            -Configuration $Configuration `
            -RuntimeIdentifier $RuntimeIdentifier `
            -RunWpfLaunchSmoke $true `
            -WpfLaunchSmokeFailOnError $true `
            -NoRestore:$NoRestore
        $lines.Add("Publish: PASS")
    } else {
        $lines.Add("Publish: SKIPPED")
    }
}
finally {
    Pop-Location
}

$wpfExe = Join-Path $repoRoot "dist\wpf\WpfHost.exe"
if (-not (Test-Path $wpfExe)) {
    $lines.Add("Status: FAIL")
    $lines.Add("Reason: WpfHost.exe not found at dist path.")
    $lines | Set-Content -Path $resolvedReportPath -Encoding UTF8
    throw "WpfHost.exe not found: $wpfExe"
}

$process = $null
if ($AutoLaunch) {
    Get-Process -Name "WpfHost" -ErrorAction SilentlyContinue | Stop-Process -Force -ErrorAction SilentlyContinue
    $process = Start-Process -FilePath $wpfExe -WorkingDirectory (Split-Path -Parent $wpfExe) -PassThru
    Start-Sleep -Seconds 2
    $lines.Add("Launch: PASS (pid=$($process.Id))")
} else {
    $existing = Get-Process -Name "WpfHost" -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($null -eq $existing) {
        $lines.Add("Launch: FAIL (AutoLaunch=false and WpfHost not running)")
        $lines.Add("Status: FAIL")
        $lines | Set-Content -Path $resolvedReportPath -Encoding UTF8
        throw "WpfHost process not found while AutoLaunch=false."
    }
    $process = $existing
    $lines.Add("Launch: USING_RUNNING_PROCESS (pid=$($process.Id))")
}

if (-not $NonInteractive.IsPresent) {
    Write-Host ""
    Write-Host "[ifacial-check] WpfHost is running. Start tracking in the app, enable iFacialMocap send, then press Enter."
    Read-Host "[ifacial-check] Press Enter when ready"
}

$samples = New-Object System.Collections.Generic.List[object]
$deadline = (Get-Date).AddSeconds([Math]::Max(5, $ObserveSeconds))
while ((Get-Date) -lt $deadline) {
    if ($null -eq $process -or $process.HasExited) {
        break
    }

    $line = Get-WpfTrackingStatusLine -ProcessId $process.Id
    $parsed = Parse-TrackingCounters -Line $line
    if ($null -ne $parsed) {
        $samples.Add([ordered]@{
            timestamp = (Get-Date).ToString("o")
            packets = $parsed.packets
            parse_err = $parsed.parse_err
            line = $parsed.line
        })
    }

    Start-Sleep -Milliseconds ([Math]::Max(200, $PollIntervalMs))
}

$status = "FAIL"
$reason = "No valid tracking status sample could be read from WPF UI automation."
$firstSample = $null
$lastSample = $null
$packetsDelta = 0
$parseErrDelta = 0

if ($samples.Count -ge 2) {
    $firstSample = $samples[0]
    $lastSample = $samples[$samples.Count - 1]
    $packetsDelta = [int64]$lastSample.packets - [int64]$firstSample.packets
    $parseErrDelta = [int64]$lastSample.parse_err - [int64]$firstSample.parse_err

    if ($packetsDelta -gt 0 -and $parseErrDelta -le 0) {
        $status = "PASS"
        $reason = "packets increased while parse_err did not increase."
    } elseif ($packetsDelta -le 0) {
        $reason = "packets did not increase."
    } else {
        $reason = "parse_err increased during observation."
    }
}
elseif (-not $NonInteractive.IsPresent) {
    Write-Host "[ifacial-check] UI automation did not capture tracking text."
    $baselineLine = Read-Host "[ifacial-check] Paste current tracking status line (with packets= and parse_err=)"
    $baselineParsed = Parse-TrackingCounters -Line $baselineLine
    if ($null -ne $baselineParsed) {
        Start-Sleep -Seconds ([Math]::Max(5, [Math]::Floor($ObserveSeconds / 2)))
        $finalLine = Read-Host "[ifacial-check] Paste tracking status line again after a few seconds"
        $finalParsed = Parse-TrackingCounters -Line $finalLine
        if ($null -ne $finalParsed) {
            $firstSample = [ordered]@{
                packets = $baselineParsed.packets
                parse_err = $baselineParsed.parse_err
                timestamp = (Get-Date).ToString("o")
            }
            $lastSample = [ordered]@{
                packets = $finalParsed.packets
                parse_err = $finalParsed.parse_err
                timestamp = (Get-Date).ToString("o")
            }
            $packetsDelta = [int64]$lastSample.packets - [int64]$firstSample.packets
            $parseErrDelta = [int64]$lastSample.parse_err - [int64]$firstSample.parse_err
            if ($packetsDelta -gt 0 -and $parseErrDelta -le 0) {
                $status = "PASS"
                $reason = "packets increased while parse_err did not increase (manual status-line fallback)."
            } elseif ($packetsDelta -le 0) {
                $reason = "packets did not increase (manual status-line fallback)."
            } else {
                $reason = "parse_err increased (manual status-line fallback)."
            }
        } else {
            $reason = "Unable to parse second manually pasted tracking status line."
        }
    } else {
        $reason = "Unable to parse manually pasted tracking status line."
    }
}

$lines.Add("")
$lines.Add("ObservationSamples: $($samples.Count)")
if ($null -ne $firstSample) {
    $lines.Add("FirstSample: packets=$($firstSample.packets) parse_err=$($firstSample.parse_err) ts=$($firstSample.timestamp)")
}
if ($null -ne $lastSample) {
    $lines.Add("LastSample: packets=$($lastSample.packets) parse_err=$($lastSample.parse_err) ts=$($lastSample.timestamp)")
}
$lines.Add("PacketsDelta: $packetsDelta")
$lines.Add("ParseErrDelta: $parseErrDelta")
$lines.Add("Status: $status")
$lines.Add("Reason: $reason")

if ($samples.Count -gt 0) {
    $lines.Add("LastTrackingLine: $($samples[$samples.Count - 1].line)")
}

$lines | Set-Content -Path $resolvedReportPath -Encoding UTF8

$json = [pscustomobject]@{
    generated_utc = [DateTimeOffset]::UtcNow.ToString("o")
    status = $status
    reason = $reason
    report_path = $resolvedReportPath
    samples = @($samples.ToArray())
    packets_delta = $packetsDelta
    parse_err_delta = $parseErrDelta
    pass_criteria = "packets increases and parse_err does not increase"
}
$json | ConvertTo-Json -Depth 6 | Set-Content -Path $resolvedJsonPath -Encoding UTF8

Write-Host "[ifacial-check] status=$status"
Write-Host "[ifacial-check] report=$resolvedReportPath"
Write-Host "[ifacial-check] json=$resolvedJsonPath"

if ($status -ne "PASS") {
    Write-Host "[ifacial-check] hint=Check app Tracking status line values: source_status, format, err."
    exit 1
}

exit 0
