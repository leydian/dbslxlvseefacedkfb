param(
    [string]$ProjectPath = ".\tools\session_state_migration_check\SessionStateMigrationCheck.csproj",
    [string]$SummaryPath = ".\build\reports\session_state_migration_check_summary.txt",
    [switch]$NoRestore
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
$resolvedProject = Resolve-AbsolutePath -Path $ProjectPath -BaseDirectory $repoRoot
$resolvedSummary = Resolve-AbsolutePath -Path $SummaryPath -BaseDirectory $repoRoot
if (-not (Test-Path $resolvedProject)) {
    throw "project not found: $resolvedProject"
}

$args = @("run", "--project", $resolvedProject, "--configuration", "Release", "--", "--summary", $resolvedSummary)
if ($NoRestore) {
    $args = @("run", "--project", $resolvedProject, "--configuration", "Release", "--no-restore", "--", "--summary", $resolvedSummary)
}

Push-Location $repoRoot
try {
    & dotnet @args | Out-Host
    if ($LASTEXITCODE -ne 0) {
        throw "session migration check failed with exit code $LASTEXITCODE"
    }
}
finally {
    Pop-Location
}

Write-Host "summary=$resolvedSummary"
