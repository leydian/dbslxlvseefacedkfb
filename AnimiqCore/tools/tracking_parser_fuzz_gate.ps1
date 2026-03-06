param(
    [string]$ProjectPath = ".\tools\tracking_parser_fuzz_gate\TrackingParserFuzzGate.csproj",
    [int]$ListenPort = 50983,
    [int]$PacketCount = 500,
    [int]$MaxDurationSeconds = 8,
    [string]$SummaryPath = ".\build\reports\tracking_parser_fuzz_gate_summary.txt",
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

$args = @(
    "run",
    "--project", $resolvedProject,
    "--configuration", "Release"
)
if ($NoRestore) {
    $args += "--no-restore"
}
$args += @(
    "--",
    "--port", "$ListenPort",
    "--packet-count", "$PacketCount",
    "--max-seconds", "$MaxDurationSeconds",
    "--summary", $resolvedSummary
)

Push-Location $repoRoot
try {
    & dotnet @args | Out-Host
    if ($LASTEXITCODE -ne 0) {
        throw "tracking parser fuzz gate failed with exit code $LASTEXITCODE"
    }
}
finally {
    Pop-Location
}

Write-Host "summary=$resolvedSummary"
