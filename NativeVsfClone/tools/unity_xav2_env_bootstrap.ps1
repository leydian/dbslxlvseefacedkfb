param(
    [string]$UnityLine = "2021-lts",
    [string]$MatrixPath = ".\tools\unity_lts_matrix.json",
    [string]$ExpectedUnityVersion = "",
    [string]$UnityEditorPath = "",
    [string]$UnityProjectPath = $env:UNITY_XAV2_PROJECT_PATH,
    [switch]$PersistUserEnv,
    [string]$ReportPath = ".\build\reports\unity_xav2_env_bootstrap_summary.txt",
    [string]$ReportJsonPath = ".\build\reports\unity_xav2_env_bootstrap_summary.json"
)

$ErrorActionPreference = "Stop"

function Resolve-AbsolutePath {
    param([string]$Path, [string]$BaseDirectory)
    if ([System.IO.Path]::IsPathRooted($Path)) {
        return [System.IO.Path]::GetFullPath($Path)
    }
    return [System.IO.Path]::GetFullPath((Join-Path $BaseDirectory $Path))
}

function Get-MatrixEntry {
    param([string]$RepoRoot, [string]$Path, [string]$Line)
    $resolved = Resolve-AbsolutePath -Path $Path -BaseDirectory $RepoRoot
    if (-not (Test-Path -LiteralPath $resolved)) {
        throw "Unity LTS matrix file not found: $resolved"
    }
    $matrix = Get-Content -Raw -Path $resolved | ConvertFrom-Json
    $entry = $matrix.PSObject.Properties[$Line].Value
    if ($null -eq $entry) {
        throw "Unity line not found in matrix: $Line"
    }
    return $entry
}

function Get-DefaultUnityEditorPath {
    param([string]$Version)
    $candidates = @(
        "C:\Program Files\Unity\Hub\Editor\$Version\Editor\Unity.exe",
        "C:\Program Files\Unity\Editor\Unity.exe"
    )
    foreach ($c in $candidates) {
        if (Test-Path $c) {
            return $c
        }
    }
    return ""
}

function Get-UnityProjectCandidates {
    param([string]$RepoRoot)
    $candidates = @()
    $searchRoots = @($RepoRoot, (Join-Path $RepoRoot "unity"), (Join-Path $RepoRoot "sample"))
    foreach ($root in $searchRoots) {
        if (-not (Test-Path $root)) { continue }
        $versions = Get-ChildItem -Path $root -Recurse -File -Filter "ProjectVersion.txt" -ErrorAction SilentlyContinue
        foreach ($v in $versions) {
            $projectRoot = Split-Path -Parent (Split-Path -Parent $v.FullName)
            if (-not [string]::IsNullOrWhiteSpace($projectRoot) -and -not ($candidates -contains $projectRoot)) {
                $candidates += $projectRoot
            }
        }
    }
    return $candidates
}

$repoRoot = Split-Path -Parent $PSScriptRoot
$entry = Get-MatrixEntry -RepoRoot $repoRoot -Path $MatrixPath -Line $UnityLine
if ([string]::IsNullOrWhiteSpace($ExpectedUnityVersion)) {
    $ExpectedUnityVersion = [string]$entry.expected_unity_version
}
$editorEnvVar = [string]$entry.editor_env_var
if ([string]::IsNullOrWhiteSpace($UnityEditorPath) -and -not [string]::IsNullOrWhiteSpace($editorEnvVar)) {
    $UnityEditorPath = [string][System.Environment]::GetEnvironmentVariable($editorEnvVar)
}

$resolvedReportPath = Resolve-AbsolutePath -Path $ReportPath -BaseDirectory $repoRoot
$resolvedReportJsonPath = Resolve-AbsolutePath -Path $ReportJsonPath -BaseDirectory $repoRoot
New-Item -ItemType Directory -Force -Path (Split-Path -Parent $resolvedReportPath) | Out-Null

$editorSource = "env"
if ([string]::IsNullOrWhiteSpace($UnityEditorPath)) {
    $UnityEditorPath = Get-DefaultUnityEditorPath -Version $ExpectedUnityVersion
    $editorSource = "auto-detect"
}
$editorResolved = ""
$editorExists = $false
if (-not [string]::IsNullOrWhiteSpace($UnityEditorPath)) {
    $editorResolved = Resolve-AbsolutePath -Path $UnityEditorPath -BaseDirectory $repoRoot
    $editorExists = Test-Path $editorResolved
}

$projectSource = "env"
if ([string]::IsNullOrWhiteSpace($UnityProjectPath)) {
    $candidates = Get-UnityProjectCandidates -RepoRoot $repoRoot
    if ($candidates.Count -gt 0) {
        $UnityProjectPath = $candidates[0]
        $projectSource = "auto-detect"
    }
}
$projectResolved = ""
$projectExists = $false
if (-not [string]::IsNullOrWhiteSpace($UnityProjectPath)) {
    $projectResolved = Resolve-AbsolutePath -Path $UnityProjectPath -BaseDirectory $repoRoot
    $projectExists = Test-Path $projectResolved
}

$setProcessEditor = $false
$setProcessProject = $false
if ($editorExists) {
    if (-not [string]::IsNullOrWhiteSpace($editorEnvVar)) {
        [System.Environment]::SetEnvironmentVariable($editorEnvVar, $editorResolved, "Process")
    }
    $setProcessEditor = $true
}
if ($projectExists) {
    [System.Environment]::SetEnvironmentVariable("UNITY_XAV2_PROJECT_PATH", $projectResolved, "Process")
    $setProcessProject = $true
}

$setUserEditor = $false
$setUserProject = $false
if ($PersistUserEnv) {
    if ($editorExists) {
        if (-not [string]::IsNullOrWhiteSpace($editorEnvVar)) {
            [System.Environment]::SetEnvironmentVariable($editorEnvVar, $editorResolved, "User")
        }
        $setUserEditor = $true
    }
    if ($projectExists) {
        [System.Environment]::SetEnvironmentVariable("UNITY_XAV2_PROJECT_PATH", $projectResolved, "User")
        $setUserProject = $true
    }
}

$status = if ($editorExists -and $projectExists) { "PASS" } else { "PARTIAL" }

$summary = [ordered]@{
    generated = (Get-Date -Format o)
    unity_line = $UnityLine
    expected_unity_version = $ExpectedUnityVersion
    editor_env_var = $editorEnvVar
    status = $status
    editor = [ordered]@{
        source = $editorSource
        value = $editorResolved
        exists = $editorExists
        process_env_set = $setProcessEditor
        user_env_set = $setUserEditor
    }
    project = [ordered]@{
        source = $projectSource
        value = $projectResolved
        exists = $projectExists
        process_env_set = $setProcessProject
        user_env_set = $setUserProject
    }
    hints = @(
        "$editorEnvVar and UNITY_XAV2_PROJECT_PATH are both required by Unity XAV2 gate scripts for line $UnityLine.",
        "If project path is missing, set UNITY_XAV2_PROJECT_PATH to a Unity project root containing ProjectSettings/ProjectVersion.txt."
    )
}

$lines = @()
$lines += "Unity XAV2 Env Bootstrap Summary"
$lines += "Generated: $($summary.generated)"
$lines += "UnityLine: $UnityLine"
$lines += "ExpectedUnityVersion: $ExpectedUnityVersion"
$lines += "EditorEnvVar: $editorEnvVar"
$lines += "Status: $status"
$lines += ""
$lines += "EditorPath: $editorResolved"
$lines += "EditorSource: $editorSource"
$lines += "EditorExists: $editorExists"
$lines += "ProcessEnvSet.Editor: $setProcessEditor"
$lines += "UserEnvSet.Editor: $setUserEditor"
$lines += ""
$lines += "ProjectPath: $projectResolved"
$lines += "ProjectSource: $projectSource"
$lines += "ProjectExists: $projectExists"
$lines += "ProcessEnvSet.Project: $setProcessProject"
$lines += "UserEnvSet.Project: $setUserProject"
$lines += ""
$lines += "Hints:"
$lines += "- $editorEnvVar and UNITY_XAV2_PROJECT_PATH are both required by Unity XAV2 gates for line $UnityLine."
$lines += "- If project path is missing, point UNITY_XAV2_PROJECT_PATH to a Unity project root (contains ProjectSettings/ProjectVersion.txt)."

$lines | Set-Content -Path $resolvedReportPath -Encoding UTF8
$summary | ConvertTo-Json -Depth 6 | Set-Content -Path $resolvedReportJsonPath -Encoding UTF8

Write-Host "summary=$resolvedReportPath"
Write-Host "json=$resolvedReportJsonPath"
if ($status -eq "PASS") {
    exit 0
}
exit 1
