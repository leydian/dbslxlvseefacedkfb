param(
    [string]$AvatarToolPath = ".\build\Release\avatar_tool.exe",
    [string]$UnityLine = "2021-lts",
    [string]$MatrixPath = ".\tools\unity_lts_matrix.json",
    [string]$UnityEditorPath = "",
    [string]$UnityProjectPath = $env:UNITY_XAV2_PROJECT_PATH,
    [string]$ExpectedUnityVersion = "",
    [string]$ReportDir = ".\build\reports",
    [string]$ReportSuffix = "",
    [string]$SampleDir = "",
    [int]$MaxExternalSamples = 5
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

if (-not (Test-Path -LiteralPath $AvatarToolPath)) {
    throw "avatar_tool not found: $AvatarToolPath"
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
    throw "Unity project path is required. Set -UnityProjectPath or UNITY_XAV2_PROJECT_PATH."
}
if (-not (Test-Path -LiteralPath $UnityProjectPath)) {
    throw "Unity project path not found: $UnityProjectPath"
}

if ([string]::IsNullOrWhiteSpace($ReportSuffix)) {
    $ReportSuffix = $UnityLine
}

function Parse-NativeProbe {
    param([string[]]$Lines)
    $row = [ordered]@{
        ParserStage = ""
        PrimaryError = ""
        MeshPayloads = -1
        MaterialPayloads = -1
        TexturePayloads = -1
        WarningCodes = -1
    }
    foreach ($line in $Lines) {
        if ($line -match '^\s*ParserStage:\s*(.+)$') { $row.ParserStage = $matches[1].Trim() }
        elseif ($line -match '^\s*PrimaryError:\s*(.+)$') { $row.PrimaryError = $matches[1].Trim() }
        elseif ($line -match '^\s*MeshPayloads:\s*(\d+)$') { $row.MeshPayloads = [int]$matches[1] }
        elseif ($line -match '^\s*MaterialPayloads:\s*(\d+)$') { $row.MaterialPayloads = [int]$matches[1] }
        elseif ($line -match '^\s*TexturePayloads:\s*(\d+)$') { $row.TexturePayloads = [int]$matches[1] }
        elseif ($line -match '^\s*WarningCodes:\s*(\d+)$') { $row.WarningCodes = [int]$matches[1] }
    }
    return [PSCustomObject]$row
}

New-Item -ItemType Directory -Force -Path $ReportDir | Out-Null
$unityLogPath = Join-Path $ReportDir "unity_xav2_${ReportSuffix}_parity_gate.log"
$unityReportPath = Join-Path $ReportDir "unity_xav2_${ReportSuffix}_parity_probe.json"
$paritySamplesDir = Join-Path $ReportDir "xav2_parity_samples_${ReportSuffix}"
$summaryTxtPath = Join-Path $ReportDir "xav2_parity_gate_${ReportSuffix}_summary.txt"
$summaryJsonPath = Join-Path $ReportDir "xav2_parity_gate_${ReportSuffix}_summary.json"

$args = @(
    "-batchmode",
    "-nographics",
    "-quit",
    "-projectPath", $UnityProjectPath,
    "-executeMethod", "VsfClone.Xav2.Editor.Xav2CiQuality.RunParityProbe",
    "-xav2ParityReportPath", $unityReportPath,
    "-xav2ParityOutputDir", $paritySamplesDir,
    "-xav2ExpectedUnityVersion", $ExpectedUnityVersion,
    "-xav2ParityMaxExternalSamples", "$MaxExternalSamples",
    "-logFile", $unityLogPath
)
if (-not [string]::IsNullOrWhiteSpace($SampleDir)) {
    $args += @("-xav2ParitySampleDir", $SampleDir)
}

& $UnityEditorPath @args
$unityExitCode = $LASTEXITCODE
if (-not (Test-Path -LiteralPath $unityReportPath)) {
    throw "Unity parity probe report not found: $unityReportPath"
}

$unityProbe = Get-Content -Raw -Path $unityReportPath | ConvertFrom-Json
$rows = [System.Collections.Generic.List[object]]::new()
$nativeProbeAllPass = $true
$parityStrictAllPass = $true

foreach ($entryRow in $unityProbe.entries) {
    $nativeOut = & $AvatarToolPath $entryRow.path
    $native = Parse-NativeProbe -Lines $nativeOut
    $nativeOk = ("$($native.PrimaryError)".ToUpperInvariant() -eq "NONE") -and
                ("$($native.ParserStage)".ToLowerInvariant().Contains("runtime-ready"))
    if (-not $nativeOk) {
        $nativeProbeAllPass = $false
    }

    $unityErrorNormalized = "$($entryRow.error_code)".ToUpperInvariant()
    $unityErrorIsNone = $unityErrorNormalized -eq "NONE"
    $parityOk = $unityErrorIsNone -and
                ($native.MeshPayloads -eq [int]$entryRow.mesh_count) -and
                ($native.MaterialPayloads -eq [int]$entryRow.material_count) -and
                ($native.TexturePayloads -eq [int]$entryRow.texture_count) -and
                ($native.WarningCodes -eq [int]$entryRow.warning_code_count)
    if (-not $parityOk) {
        $parityStrictAllPass = $false
    }

    $rows.Add([PSCustomObject]@{
        name = [string]$entryRow.name
        path = [string]$entryRow.path
        unity_ok = [bool]$entryRow.ok
        unity_error = [string]$entryRow.error_code
        unity_stage = [string]$entryRow.parser_stage
        unity_mesh = [int]$entryRow.mesh_count
        unity_material = [int]$entryRow.material_count
        unity_texture = [int]$entryRow.texture_count
        unity_warning_codes = [int]$entryRow.warning_code_count
        native_error = [string]$native.PrimaryError
        native_stage = [string]$native.ParserStage
        native_mesh_payloads = [int]$native.MeshPayloads
        native_material_payloads = [int]$native.MaterialPayloads
        native_texture_payloads = [int]$native.TexturePayloads
        native_warning_codes = [int]$native.WarningCodes
        native_probe_status = if ($nativeOk) { "PASS" } else { "FAIL" }
        parity_status = if ($parityOk) { "PASS" } else { "FAIL" }
    })
}

$gateP1 = ($unityExitCode -eq 0) -and ([string]$unityProbe.overall_status -eq "PASS")
$gateP2 = $nativeProbeAllPass
$gateP3 = $parityStrictAllPass
$overall = $gateP1 -and $gateP2 -and $gateP3

$summary = [ordered]@{
    unity_line = $UnityLine
    generated = (Get-Date).ToString("s")
    unity_version = [string]$unityProbe.unity_version
    unity_exit_code = $unityExitCode
    gates = [ordered]@{
        gate_p1_unity_probe = if ($gateP1) { "PASS" } else { "FAIL" }
        gate_p2_native_probe = if ($gateP2) { "PASS" } else { "FAIL" }
        gate_p3_unity_native_parity = if ($gateP3) { "PASS" } else { "FAIL" }
        overall = if ($overall) { "PASS" } else { "FAIL" }
    }
    source_report = $unityReportPath
    unity_log = $unityLogPath
    rows = $rows
}

$summary | ConvertTo-Json -Depth 8 | Set-Content -Path $summaryJsonPath -Encoding UTF8

$lines = @()
$lines += "XAV2 Unity/Native Parity Gate Summary"
$lines += "Generated: $($summary.generated)"
$lines += "UnityLine: $UnityLine"
$lines += "UnityVersion: $($summary.unity_version)"
$lines += "UnityExitCode: $unityExitCode"
$lines += ""
$lines += "Gate Results"
$lines += "- GateP1 (unity probe pass): $($summary.gates.gate_p1_unity_probe)"
$lines += "- GateP2 (native probe pass): $($summary.gates.gate_p2_native_probe)"
$lines += "- GateP3 (unity/native parity): $($summary.gates.gate_p3_unity_native_parity)"
$lines += "- Overall: $($summary.gates.overall)"
$lines += ""
$lines += "Rows"
foreach ($row in $rows) {
    $lines += "- $($row.name): unity=($($row.unity_error), mesh=$($row.unity_mesh), mat=$($row.unity_material), tex=$($row.unity_texture), warn=$($row.unity_warning_codes)) native=($($row.native_error), mesh=$($row.native_mesh_payloads), mat=$($row.native_material_payloads), tex=$($row.native_texture_payloads), warn=$($row.native_warning_codes)) parity=$($row.parity_status)"
}
$lines += ""
$lines += "Artifacts"
$lines += "- unity_report=$unityReportPath"
$lines += "- unity_log=$unityLogPath"
$lines += "- summary_json=$summaryJsonPath"
$lines | Set-Content -Path $summaryTxtPath -Encoding UTF8

if ($UnityLine -eq "2021-lts") {
    Copy-Item -Force -Path $unityLogPath -Destination (Join-Path $ReportDir "unity_xav2_parity_gate.log")
    Copy-Item -Force -Path $unityReportPath -Destination (Join-Path $ReportDir "unity_xav2_parity_probe.json")
    Copy-Item -Force -Path $summaryJsonPath -Destination (Join-Path $ReportDir "xav2_parity_gate_summary.json")
    Copy-Item -Force -Path $summaryTxtPath -Destination (Join-Path $ReportDir "xav2_parity_gate_summary.txt")
}

if (-not $overall) {
    exit 1
}
exit 0
