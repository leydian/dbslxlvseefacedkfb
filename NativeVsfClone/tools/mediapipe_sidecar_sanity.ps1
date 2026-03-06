param(
    [string]$PythonExe = "",
    [string]$SidecarScript = ".\tools\mediapipe_webcam_sidecar.py",
    [switch]$SkipImportProbe,
    [switch]$RequireExplicitPythonExe,
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

function Resolve-PythonExecutable {
    param(
        [string]$CliValue,
        [bool]$RequireExplicit
    )

    $trimmedCli = $CliValue.Trim()
    if (-not [string]::IsNullOrWhiteSpace($trimmedCli)) {
        return [PSCustomObject]@{
            Executable = $trimmedCli
            Source = "cli"
            IsExplicit = $true
        }
    }

    $envValue = [Environment]::GetEnvironmentVariable("VSFCLONE_MEDIAPIPE_PYTHON")
    if (-not [string]::IsNullOrWhiteSpace($envValue)) {
        return [PSCustomObject]@{
            Executable = $envValue.Trim()
            Source = "env:VSFCLONE_MEDIAPIPE_PYTHON"
            IsExplicit = $true
        }
    }

    if ($RequireExplicit) {
        return [PSCustomObject]@{
            Executable = ""
            Source = "missing"
            IsExplicit = $false
        }
    }

    return [PSCustomObject]@{
        Executable = "python"
        Source = "default"
        IsExplicit = $false
    }
}

$repoRoot = Split-Path -Parent $PSScriptRoot
$resolvedScript = Resolve-AbsolutePath -Path $SidecarScript -BaseDirectory $repoRoot
$resolvedSummary = Resolve-AbsolutePath -Path $SummaryPath -BaseDirectory $repoRoot
New-Item -ItemType Directory -Force -Path (Split-Path -Parent $resolvedSummary) | Out-Null

$resolvedPython = Resolve-PythonExecutable -CliValue $PythonExe -RequireExplicit:$RequireExplicitPythonExe
$resolvedPythonExe = $resolvedPython.Executable

$checks = [System.Collections.Generic.List[string]]::new()
$overall = $true

if (Test-Path $resolvedScript) {
    $checks.Add("- sidecar_script: PASS ($resolvedScript)")
} else {
    $checks.Add("- sidecar_script: FAIL ($resolvedScript)")
    $overall = $false
}

$checks.Add("- python_source: $($resolvedPython.Source)")
if ([string]::IsNullOrWhiteSpace($resolvedPythonExe)) {
    $checks.Add("- python_executable: FAIL (missing explicit python executable; set VSFCLONE_MEDIAPIPE_PYTHON or pass -PythonExe)")
    $overall = $false
} else {
    try {
        & $resolvedPythonExe --version | Out-Null
        if ($LASTEXITCODE -eq 0) {
            $checks.Add("- python_executable: PASS ($resolvedPythonExe)")
        } else {
            $checks.Add("- python_executable: FAIL ($resolvedPythonExe)")
            $overall = $false
        }
    }
    catch {
        $checks.Add("- python_executable: FAIL ($resolvedPythonExe): $($_.Exception.Message)")
        $overall = $false
    }
}

if (-not $SkipImportProbe -and -not [string]::IsNullOrWhiteSpace($resolvedPythonExe)) {
    try {
        & $resolvedPythonExe -c "import mediapipe, cv2; print('ok')" | Out-Null
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
$lines.Add("PythonExe: $resolvedPythonExe")
$lines.Add("PythonSource: $($resolvedPython.Source)")
$lines.Add("RequireExplicitPythonExe: $RequireExplicitPythonExe")
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
