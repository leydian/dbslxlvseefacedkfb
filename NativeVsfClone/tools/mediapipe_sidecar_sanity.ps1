param(
    [string]$PythonExe = "python",
    [string]$SidecarScript = ".\tools\mediapipe_webcam_sidecar.py",
    [switch]$SkipImportProbe,
    [string]$SummaryPath = ".\build\reports\mediapipe_sidecar_sanity_summary.txt"
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
$resolvedScript = Resolve-AbsolutePath -Path $SidecarScript -BaseDirectory $repoRoot
$resolvedSummary = Resolve-AbsolutePath -Path $SummaryPath -BaseDirectory $repoRoot
New-Item -ItemType Directory -Force -Path (Split-Path -Parent $resolvedSummary) | Out-Null

$checks = [System.Collections.Generic.List[string]]::new()
$overall = $true

if (Test-Path $resolvedScript) {
    $checks.Add("- sidecar_script: PASS ($resolvedScript)")
} else {
    $checks.Add("- sidecar_script: FAIL ($resolvedScript)")
    $overall = $false
}

try {
    & $PythonExe --version | Out-Null
    if ($LASTEXITCODE -eq 0) {
        $checks.Add("- python_executable: PASS ($PythonExe)")
    } else {
        $checks.Add("- python_executable: FAIL ($PythonExe)")
        $overall = $false
    }
}
catch {
    $checks.Add("- python_executable: FAIL ($PythonExe): $($_.Exception.Message)")
    $overall = $false
}

if (-not $SkipImportProbe) {
    try {
        & $PythonExe -c "import mediapipe, cv2; print('ok')" | Out-Null
        if ($LASTEXITCODE -eq 0) {
            $checks.Add("- python_import_probe: PASS (mediapipe+cv2)")
        } else {
            $checks.Add("- python_import_probe: FAIL (mediapipe+cv2)")
            $overall = $false
        }
    }
    catch {
        $checks.Add("- python_import_probe: FAIL ($($_.Exception.Message))")
        $overall = $false
    }
}

$lines = [System.Collections.Generic.List[string]]::new()
$lines.Add("MediaPipe Sidecar Sanity Summary")
$lines.Add("Generated: $(Get-Date -Format o)")
$lines.Add("PythonExe: $PythonExe")
$lines.Add("SidecarScript: $resolvedScript")
$lines.Add("SkipImportProbe: $SkipImportProbe")
$lines.Add("")
$lines.Add("Checks:")
foreach ($check in $checks) { $lines.Add($check) }
$lines.Add("")
$lines.Add("Overall: $(if ($overall) { 'PASS' } else { 'FAIL' })")
$lines | Set-Content -Path $resolvedSummary -Encoding UTF8
Write-Host "summary=$resolvedSummary"

if (-not $overall) {
    exit 1
}
