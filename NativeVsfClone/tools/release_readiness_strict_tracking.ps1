param(
    [string]$MediapipePythonExe = ".\.venv\Scripts\python.exe",
    [string]$Configuration = "Release",
    [string]$RuntimeIdentifier = "win-x64",
    [switch]$SkipNativeBuild,
    [switch]$NoRestore,
    [switch]$IncludeWinUi
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
$resolvedPythonExe = Resolve-AbsolutePath -Path $MediapipePythonExe -BaseDirectory $repoRoot
if (-not (Test-Path $resolvedPythonExe)) {
    throw "Mediapipe python executable not found: $resolvedPythonExe"
}

Push-Location $repoRoot
try {
    $args = @(
        "-ExecutionPolicy", "Bypass",
        "-File", ".\tools\release_readiness_gate.ps1",
        "-Configuration", $Configuration,
        "-RuntimeIdentifier", $RuntimeIdentifier,
        "-MediapipePythonExe", $resolvedPythonExe
    )
    if ($SkipNativeBuild) { $args += "-SkipNativeBuild" }
    if ($NoRestore) { $args += "-NoRestore" }
    if ($IncludeWinUi) { $args += "-IncludeWinUi" }

    & powershell @args
    if ($LASTEXITCODE -ne 0) {
        throw "release_readiness_gate failed (exit=$LASTEXITCODE)"
    }

    & powershell -ExecutionPolicy Bypass -File .\tools\release_gate_dashboard.ps1
    if ($LASTEXITCODE -ne 0) {
        throw "release_gate_dashboard refresh failed (exit=$LASTEXITCODE)"
    }
}
finally {
    Pop-Location
}

Write-Host "status=PASS"
Write-Host "python=$resolvedPythonExe"
