param(
    [string]$UnityEditorPath = $env:UNITY_2021_3_18F1_EDITOR_PATH,
    [string]$UnityProjectPath = $env:UNITY_XAV2_PROJECT_PATH,
    [string]$ExpectedUnityVersion = "2021.3.18f1",
    [string]$ReportDir = ".\build\reports"
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

function New-ReportObject {
    param([string]$UnityVersion)
    return [ordered]@{
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

if ([string]::IsNullOrWhiteSpace($UnityEditorPath)) {
    throw "Unity editor path is required. Set -UnityEditorPath or UNITY_2021_3_18F1_EDITOR_PATH."
}
if (-not (Test-Path -LiteralPath $UnityEditorPath)) {
    throw "Unity editor executable not found: $UnityEditorPath"
}
if ([string]::IsNullOrWhiteSpace($UnityProjectPath)) {
    throw "Unity project path is required. Set -UnityProjectPath or UNITY_XAV2_PROJECT_PATH."
}
if (-not (Test-Path -LiteralPath $UnityProjectPath)) {
    throw "Unity project path not found: $UnityProjectPath"
}

New-Item -ItemType Directory -Force -Path $ReportDir | Out-Null

$editModeLogPath = Join-Path $ReportDir "unity_xav2_editmode.log"
$editModeResultXmlPath = Join-Path $ReportDir "unity_xav2_editmode_results.xml"
$smokeLogPath = Join-Path $ReportDir "unity_xav2_smoke.log"
$smokeReportPath = Join-Path $ReportDir "unity_xav2_smoke_report.json"
$smokeOutputPath = Join-Path $ReportDir "unity_xav2_smoke_output.xav2"
$summaryJsonPath = Join-Path $ReportDir "unity_xav2_validation_summary.json"
$summaryTxtPath = Join-Path $ReportDir "unity_xav2_validation_summary.txt"

$summary = New-ReportObject -UnityVersion $ExpectedUnityVersion

Write-Host "[unity_xav2_validate] step=editmode_tests unity=$ExpectedUnityVersion"
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

Write-Host "[unity_xav2_validate] step=smoke_export_load unity=$ExpectedUnityVersion"
$smokeArgs = @(
    "-batchmode",
    "-nographics",
    "-quit",
    "-projectPath", $UnityProjectPath,
    "-executeMethod", "VsfClone.Xav2.Editor.Xav2CiSmoke.Run",
    "-xav2SmokeOutputPath", $smokeOutputPath,
    "-xav2SmokeReportPath", $smokeReportPath,
    "-xav2ExpectedUnityVersion", $ExpectedUnityVersion,
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

if ($summary.overall_status -ne "PASS") {
    Write-Host "[unity_xav2_validate] FAIL"
    exit 1
}

Write-Host "[unity_xav2_validate] PASS"
exit 0
