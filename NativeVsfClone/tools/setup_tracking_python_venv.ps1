param(
    [string]$VenvPath = ".\.venv",
    [string]$PythonExe = "",
    [switch]$InstallPythonWithWinget,
    [string]$WingetPythonPackageId = "Python.Python.3.11"
)

$ErrorActionPreference = "Stop"

function Resolve-AbsolutePath {
    param([string]$Path, [string]$BaseDirectory)
    if ([System.IO.Path]::IsPathRooted($Path)) {
        return [System.IO.Path]::GetFullPath($Path)
    }
    return [System.IO.Path]::GetFullPath((Join-Path $BaseDirectory $Path))
}

function Find-RealPythonExe {
    param([string]$Candidate)
    if (-not [string]::IsNullOrWhiteSpace($Candidate)) {
        if (-not (Test-Path $Candidate)) {
            throw "Python executable not found: $Candidate"
        }
        return (Resolve-Path $Candidate).Path
    }

    $envPinned = [Environment]::GetEnvironmentVariable("VSFCLONE_MEDIAPIPE_PYTHON")
    if (-not [string]::IsNullOrWhiteSpace($envPinned) -and (Test-Path $envPinned)) {
        return (Resolve-Path $envPinned).Path
    }

    $candidates = @(
        "C:\Python311\python.exe",
        "C:\Python312\python.exe",
        "C:\Python310\python.exe",
        "$env:LOCALAPPDATA\Programs\Python\Python311\python.exe",
        "$env:LOCALAPPDATA\Programs\Python\Python312\python.exe",
        "$env:LOCALAPPDATA\Programs\Python\Python310\python.exe"
    )
    foreach ($c in $candidates) {
        if (-not [string]::IsNullOrWhiteSpace($c) -and (Test-Path $c)) {
            return (Resolve-Path $c).Path
        }
    }

    $cmd = Get-Command python -ErrorAction SilentlyContinue
    if ($null -ne $cmd -and -not [string]::IsNullOrWhiteSpace($cmd.Source)) {
        $src = $cmd.Source
        if ($src -notlike "*\\Microsoft\\WindowsApps\\*") {
            return $src
        }
    }

    return ""
}

$repoRoot = Split-Path -Parent $PSScriptRoot
$resolvedVenvPath = Resolve-AbsolutePath -Path $VenvPath -BaseDirectory $repoRoot

$resolvedPython = Find-RealPythonExe -Candidate $PythonExe
if ([string]::IsNullOrWhiteSpace($resolvedPython) -and $InstallPythonWithWinget) {
    $winget = Get-Command winget -ErrorAction SilentlyContinue
    if ($null -eq $winget) {
        throw "winget not found; cannot auto-install Python."
    }
    Write-Host "[tracking-venv] Installing Python via winget package=$WingetPythonPackageId"
    & winget install --id $WingetPythonPackageId --exact --silent --accept-package-agreements --accept-source-agreements
    if ($LASTEXITCODE -ne 0) {
        throw "winget python install failed (exit=$LASTEXITCODE)"
    }
    $resolvedPython = Find-RealPythonExe -Candidate ""
}

if ([string]::IsNullOrWhiteSpace($resolvedPython)) {
    throw "No usable Python executable found. Pass -PythonExe or run with -InstallPythonWithWinget."
}

Write-Host "[tracking-venv] PythonExe=$resolvedPython"
Write-Host "[tracking-venv] VenvPath=$resolvedVenvPath"

& $resolvedPython -m venv $resolvedVenvPath
if ($LASTEXITCODE -ne 0) {
    throw "venv creation failed (exit=$LASTEXITCODE)"
}

$venvPython = Join-Path $resolvedVenvPath "Scripts\python.exe"
if (-not (Test-Path $venvPython)) {
    throw "venv python not found: $venvPython"
}

& $venvPython -m pip install --upgrade pip
if ($LASTEXITCODE -ne 0) {
    throw "pip upgrade failed (exit=$LASTEXITCODE)"
}

& $venvPython -m pip install mediapipe opencv-python
if ($LASTEXITCODE -ne 0) {
    throw "pip install mediapipe/opencv-python failed (exit=$LASTEXITCODE)"
}

& $venvPython -c "import mediapipe, cv2; print('ok')"
if ($LASTEXITCODE -ne 0) {
    throw "import probe failed (mediapipe/cv2)"
}

Write-Host "venv_python=$venvPython"
