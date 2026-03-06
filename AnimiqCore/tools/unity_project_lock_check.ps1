param(
    [string]$UnityProjectPath = $env:UNITY_MIQ_PROJECT_PATH,
    [string]$OutputJson = ".\build\reports\unity_project_lock_check.json",
    [string]$OutputTxt = ".\build\reports\unity_project_lock_check.txt"
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

function Resolve-AbsolutePath {
    param([string]$Path, [string]$BaseDirectory)
    if ([System.IO.Path]::IsPathRooted($Path)) {
        return [System.IO.Path]::GetFullPath($Path)
    }
    return [System.IO.Path]::GetFullPath((Join-Path $BaseDirectory $Path))
}

$repoRoot = Split-Path -Parent $PSScriptRoot
$resolvedJson = Resolve-AbsolutePath -Path $OutputJson -BaseDirectory $repoRoot
$resolvedTxt = Resolve-AbsolutePath -Path $OutputTxt -BaseDirectory $repoRoot
New-Item -ItemType Directory -Force -Path (Split-Path -Parent $resolvedJson) | Out-Null
New-Item -ItemType Directory -Force -Path (Split-Path -Parent $resolvedTxt) | Out-Null

if ([string]::IsNullOrWhiteSpace($UnityProjectPath)) {
    throw "Unity project path is required. Set -UnityProjectPath or UNITY_MIQ_PROJECT_PATH."
}

$resolvedProject = Resolve-AbsolutePath -Path $UnityProjectPath -BaseDirectory $repoRoot
if (-not (Test-Path -LiteralPath $resolvedProject)) {
    throw "Unity project path not found: $resolvedProject"
}

$lockFile = Join-Path $resolvedProject "Temp\UnityLockfile"
$lockExists = Test-Path -LiteralPath $lockFile

$unityProcs = @(Get-CimInstance Win32_Process -Filter "Name = 'Unity.exe'" -ErrorAction SilentlyContinue)
$projectToken = $resolvedProject.ToLowerInvariant()
$matchingProc = @()
foreach ($proc in $unityProcs) {
    $cmd = [string]$proc.CommandLine
    if ([string]::IsNullOrWhiteSpace($cmd)) { continue }
    if ($cmd.ToLowerInvariant().Contains($projectToken)) {
        $matchingProc += $proc
    }
}

$isLocked = $lockExists -or $matchingProc.Count -gt 0
$reason = if ($isLocked) { "PROJECT_LOCKED_BY_ANOTHER_UNITY_INSTANCE" } else { "NONE" }

$obj = [ordered]@{
    generated_utc = (Get-Date).ToUniversalTime().ToString("o")
    unity_project_path = $resolvedProject
    lock_file = $lockFile
    lock_file_exists = [bool]$lockExists
    matching_unity_process_count = [int]$matchingProc.Count
    matching_unity_processes = @($matchingProc | ForEach-Object {
        [ordered]@{
            pid = [int]$_.ProcessId
            command_line = [string]$_.CommandLine
        }
    })
    locked = [bool]$isLocked
    reason = $reason
}
$obj | ConvertTo-Json -Depth 6 | Set-Content -Path $resolvedJson -Encoding UTF8

$lines = @(
    "Unity Project Lock Check"
    "GeneratedUtc: $($obj.generated_utc)"
    "UnityProjectPath: $resolvedProject"
    "LockFileExists: $lockExists"
    "MatchingUnityProcessCount: $($matchingProc.Count)"
    "Locked: $isLocked"
    "Reason: $reason"
    "OutputJson: $resolvedJson"
)
$lines | Set-Content -Path $resolvedTxt -Encoding UTF8

Write-Host "json=$resolvedJson"
Write-Host "txt=$resolvedTxt"

if ($isLocked) {
    exit 1
}

