param(
    [string]$PythonExe = "AnimiqCore\.venv\Scripts\python.exe",
    [string]$ScriptPath = "AnimiqCore\tools\mediapipe_webcam_sidecar.py",
    [int]$DurationSec = 15,
    [int]$TargetFps = 60
)

$ErrorActionPreference = "Continue"

Write-Host "[benchmark] Starting MediaPipe Performance Benchmark ($DurationSec sec, Target: $TargetFps fps)..." -ForegroundColor Cyan

$processInfo = New-Object System.Diagnostics.ProcessStartInfo
$processInfo.FileName = $PythonExe
$processInfo.Arguments = "$ScriptPath --fps $TargetFps --camera 0"
$processInfo.RedirectStandardOutput = $true
$processInfo.RedirectStandardError = $true
$processInfo.UseShellExecute = $false
$processInfo.CreateNoWindow = $true

$process = [System.Diagnostics.Process]::Start($processInfo)
$startTime = Get-Date
$cpuSamples = @()
$inferenceSamples = @()
$fpsSamples = @()

$logPath = "build\reports\mediapipe_bench_raw.txt"
New-Item -ItemType File -Path $logPath -Force | Out-Null

Write-Host "[benchmark] Monitoring Process ID: $($process.Id)"

while ((Get-Date) -lt $startTime.AddSeconds($DurationSec)) {
    if ($process.HasExited) { break }
    
    # Sample CPU usage
    try {
        $cpu = Get-Counter "\Process($($process.ProcessName)*)\% Processor Time" -ErrorAction SilentlyContinue
        if ($cpu) {
            $cpuVal = ($cpu.CounterSamples | Measure-Object -Property CookedValue -Sum).Sum / [Environment]::ProcessorCount
            $cpuSamples += $cpuVal
        }
    } catch {}

    # Read output
    while ($process.StandardOutput.Peek() -ne -1) {
        $line = $process.StandardOutput.ReadLine()
        if ($line -match '\{.*\}') {
            try {
                $data = $line | ConvertFrom-Json
                $inferenceSamples += $data.inference_ms
                $fpsSamples += $data.capture_fps
                $line | Out-File -FilePath $logPath -Append
            } catch {}
        }
    }
    
    Start-Sleep -Milliseconds 500
}

if (!$process.HasExited) {
    Write-Host "[benchmark] Stopping process..."
    Stop-Process -Id $process.Id -Force
}

# Calculate statistics
function Get-Stats($arr) {
    if ($arr.Count -eq 0) { return @{avg=0; min=0; max=0} }
    $sorted = $arr | Sort-Object
    $avg = ($arr | Measure-Object -Average).Average
    return @{
        avg = [math]::Round($avg, 2)
        min = [math]::Round($sorted[0], 2)
        max = [math]::Round($sorted[-1], 2)
    }
}

$cpuStats = Get-Stats $cpuSamples
$infStats = Get-Stats $inferenceSamples
$fpsStats = Get-Stats $fpsSamples

$summaryPath = "build\reports\mediapipe_benchmark_summary.txt"
$lines = @()
$lines += "MediaPipe Tracking Performance Benchmark"
$lines += "========================================"
$lines += "Timestamp: $(Get-Date -Format o)"
$lines += "Duration: $DurationSec sec"
$lines += "Target FPS: $TargetFps"
$lines += ""
$lines += "CPU Usage (% Total):"
$lines += "  Avg: $($cpuStats.avg)%"
$lines += "  Min: $($cpuStats.min)%"
$lines += "  Max: $($cpuStats.max)%"
$lines += ""
$lines += "Inference Latency (ms):"
$lines += "  Avg: $($infStats.avg) ms"
$lines += "  Min: $($infStats.min) ms"
$lines += "  Max: $($infStats.max) ms"
$lines += ""
$lines += "Capture/Output FPS:"
$lines += "  Avg: $($fpsStats.avg) fps"
$lines += "  Min: $($fpsStats.min) fps"
$lines += "  Max: $($fpsStats.max) fps"
$lines += ""
$lines += "Status: " + (if ($fpsStats.avg -ge ($TargetFps * 0.9) -and $cpuStats.avg -le 15) { "PASS" } else { "FAIL/NEEDS_OPTIMIZATION" })

$lines | Set-Content -Path $summaryPath -Encoding UTF8
Write-Host ($lines -join "`n")
Write-Host "`n[benchmark] Summary saved to: $summaryPath" -ForegroundColor Green
