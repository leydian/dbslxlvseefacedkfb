param(
    [string]$UnityLine = "2021-lts",
    [string]$MatrixPath = ".\tools\unity_lts_matrix.json",
    [string]$UnityEditorPath = "",
    [string]$UnityProjectPath = $env:UNITY_MIQ_PROJECT_PATH,
    [string]$ExpectedUnityVersion = "",
    [string]$ReportDir = ".\build\reports",
    [string]$ReportSuffix = ""
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

function New-ReportObject {
    param([string]$UnityVersion)
    return [ordered]@{
        unity_line          = $UnityLine
        unity_version       = $UnityVersion
        tests_passed        = $false
        export_smoke_passed = $false
        load_smoke_passed   = $false
        overall_status      = "FAIL"
        generated_at        = (Get-Date).ToString("yyyy-MM-ddTHH:mm:ssK")
    }
}

function Write-JsonFile {
    param(
        [Parameter(Mandatory = $true)][string]$Path,
        [Parameter(Mandatory = $true)]$Value
    )
    $dir = Split-Path -Parent $Path
    if (-not [string]::IsNullOrWhiteSpace($dir)) {
        New-Item -ItemType Directory -Force -Path $dir | Out-Null
    }
    ($Value | ConvertTo-Json -Depth 8) | Set-Content -Path $Path -Encoding UTF8
}

function Invoke-Unity {
    param(
        [Parameter(Mandatory = $true)][string]$EditorPath,
        [Parameter(Mandatory = $true)][string[]]$Args
    )
    & $EditorPath @Args
    return $LASTEXITCODE
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

if ([string]::IsNullOrWhiteSpace($UnityEditorPath)) {
    throw "Unity editor path is required. Set -UnityEditorPath or env:$editorEnvVar."
}
if (-not (Test-Path -LiteralPath $UnityEditorPath)) {
    throw "Unity editor executable not found: $UnityEditorPath"
}
if ([string]::IsNullOrWhiteSpace($UnityProjectPath)) {
    throw "Unity project path is required. Set -UnityProjectPath or UNITY_MIQ_PROJECT_PATH."
}
if (-not (Test-Path -LiteralPath $UnityProjectPath)) {
    throw "Unity project path not found: $UnityProjectPath"
}

if ([string]::IsNullOrWhiteSpace($ReportSuffix)) {
    $ReportSuffix = $UnityLine
}

New-Item -ItemType Directory -Force -Path $ReportDir | Out-Null

$editModeLogPath = Join-Path $ReportDir "unity_miq_${ReportSuffix}_editmode.log"
$editModeResultXmlPath = Join-Path $ReportDir "unity_miq_${ReportSuffix}_editmode_results.xml"
$smokeLogPath = Join-Path $ReportDir "unity_miq_${ReportSuffix}_smoke.log"
$smokeReportPath = Join-Path $ReportDir "unity_miq_${ReportSuffix}_smoke_report.json"
$smokeOutputPath = Join-Path $ReportDir "unity_miq_${ReportSuffix}_smoke_output.miq"
$summaryJsonPath = Join-Path $ReportDir "unity_miq_${ReportSuffix}_validation_summary.json"
$summaryTxtPath = Join-Path $ReportDir "unity_miq_${ReportSuffix}_validation_summary.txt"

$summary = New-ReportObject -UnityVersion $ExpectedUnityVersion

Write-Host "[unity_miq_validate] line=$UnityLine step=editmode_tests unity=$ExpectedUnityVersion"
$testArgs = @(
    "-batchmode",
    "-nographics",
    "-quit",
    "-projectPath", $UnityProjectPath,
    "-runTests",
    "-testPlatform", "EditMode",
    "-testResults", $editModeResultXmlPath,
    "-logFile", $editModeLogPath
)
$testExitCode = Invoke-Unity -EditorPath $UnityEditorPath -Args $testArgs
$summary.tests_passed = ($testExitCode -eq 0)
if (-not (Test-Path -LiteralPath $editModeResultXmlPath)) {
    $summary.tests_passed = $false
}

Write-Host "[unity_miq_validate] line=$UnityLine step=smoke_export_load unity=$ExpectedUnityVersion"
$smokeArgs = @(
    "-batchmode",
    "-nographics",
    "-quit",
    "-projectPath", $UnityProjectPath,
    "-executeMethod", "Animiq.Miq.Editor.MiqCiSmoke.Run",
    "-miqSmokeOutputPath", $smokeOutputPath,
    "-miqSmokeReportPath", $smokeReportPath,
    "-miqExpectedUnityVersion", $ExpectedUnityVersion,
    "-logFile", $smokeLogPath
)
$smokeExitCode = Invoke-Unity -EditorPath $UnityEditorPath -Args $smokeArgs
$smokeReport = $null
if (Test-Path -LiteralPath $smokeReportPath) {
    $smokeReport = Get-Content -Raw -Path $smokeReportPath | ConvertFrom-Json
    $summary.unity_version = [string]$smokeReport.unity_version
    $summary.export_smoke_passed = [bool]$smokeReport.export_smoke_passed
    $summary.load_smoke_passed = [bool]$smokeReport.load_smoke_passed
} else {
    $summary.export_smoke_passed = $false
    $summary.load_smoke_passed = $false
}
if ($smokeExitCode -ne 0) {
    $summary.export_smoke_passed = $false
    $summary.load_smoke_passed = $false
}

$summary.overall_status = if ($summary.tests_passed -and $summary.export_smoke_passed -and $summary.load_smoke_passed) {
    "PASS"
} else {
    "FAIL"
}

Write-JsonFile -Path $summaryJsonPath -Value $summary

$summaryLines = @(
    "unity_line=$UnityLine",
    "unity_version=$($summary.unity_version)",
    "tests_passed=$($summary.tests_passed)",
    "export_smoke_passed=$($summary.export_smoke_passed)",
    "load_smoke_passed=$($summary.load_smoke_passed)",
    "overall_status=$($summary.overall_status)",
    "editmode_log=$editModeLogPath",
    "editmode_results=$editModeResultXmlPath",
    "smoke_log=$smokeLogPath",
    "smoke_report=$smokeReportPath"
)
$summaryLines | Set-Content -Path $summaryTxtPath -Encoding UTF8

if ($UnityLine -eq "2021-lts") {
    Copy-Item -Force -Path $editModeLogPath -Destination (Join-Path $ReportDir "unity_miq_editmode.log")
    Copy-Item -Force -Path $editModeResultXmlPath -Destination (Join-Path $ReportDir "unity_miq_editmode_results.xml")
    Copy-Item -Force -Path $smokeLogPath -Destination (Join-Path $ReportDir "unity_miq_smoke.log")
    Copy-Item -Force -Path $smokeReportPath -Destination (Join-Path $ReportDir "unity_miq_smoke_report.json")
    Copy-Item -Force -Path $summaryJsonPath -Destination (Join-Path $ReportDir "unity_miq_validation_summary.json")
    Copy-Item -Force -Path $summaryTxtPath -Destination (Join-Path $ReportDir "unity_miq_validation_summary.txt")
}

if ($summary.overall_status -ne "PASS") {
    Write-Host "[unity_miq_validate] line=$UnityLine FAIL"
    exit 1
}

Write-Host "[unity_miq_validate] line=$UnityLine PASS"
exit 0
