param(
    [string]$UnityLine = "2021-lts",
    [string]$MatrixPath = ".\tools\unity_lts_matrix.json",
    [string]$UnityEditorPath = "",
    [string]$UnityProjectPath = $env:UNITY_MIQ_PROJECT_PATH,
    [string]$AvatarToolPath = ".\build\Release\avatar_tool.exe",
    [string]$ReportDir = ".\build\reports",
    [string]$ReportSuffix = ""
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

function Invoke-Step {
    param([string]$Name, [scriptblock]$Action)
    Write-Host "[unity_miq_gate_bundle] START: $Name"
    & $Action
    if ($LASTEXITCODE -ne 0) {
        throw "$Name failed (exit=$LASTEXITCODE)"
    }
    Write-Host "[unity_miq_gate_bundle] PASS: $Name"
}

$repoRoot = Split-Path -Parent $PSScriptRoot
Push-Location $repoRoot
try {
    Invoke-Step -Name "Project lock preflight" -Action {
        & powershell -ExecutionPolicy Bypass -File .\tools\unity_project_lock_check.ps1 -UnityProjectPath $UnityProjectPath
    }

    Invoke-Step -Name "Unity MIQ validate" -Action {
        $args = @(
            "-ExecutionPolicy", "Bypass",
            "-File", ".\tools\unity_Miq_validate.ps1",
            "-UnityLine", $UnityLine,
            "-MatrixPath", $MatrixPath,
            "-UnityProjectPath", $UnityProjectPath,
            "-ReportDir", $ReportDir
        )
        if (-not [string]::IsNullOrWhiteSpace($UnityEditorPath)) { $args += @("-UnityEditorPath", $UnityEditorPath) }
        if (-not [string]::IsNullOrWhiteSpace($ReportSuffix)) { $args += @("-ReportSuffix", $ReportSuffix) }
        & powershell @args
    }

    Invoke-Step -Name "MIQ compression gate" -Action {
        $args = @(
            "-ExecutionPolicy", "Bypass",
            "-File", ".\tools\Miq_compression_quality_gate.ps1",
            "-UnityLine", $UnityLine,
            "-MatrixPath", $MatrixPath,
            "-UnityProjectPath", $UnityProjectPath,
            "-ReportDir", $ReportDir
        )
        if (-not [string]::IsNullOrWhiteSpace($UnityEditorPath)) { $args += @("-UnityEditorPath", $UnityEditorPath) }
        if (-not [string]::IsNullOrWhiteSpace($ReportSuffix)) { $args += @("-ReportSuffix", $ReportSuffix) }
        & powershell @args
    }

    Invoke-Step -Name "MIQ parity gate" -Action {
        $args = @(
            "-ExecutionPolicy", "Bypass",
            "-File", ".\tools\Miq_parity_gate.ps1",
            "-UnityLine", $UnityLine,
            "-MatrixPath", $MatrixPath,
            "-UnityProjectPath", $UnityProjectPath,
            "-AvatarToolPath", $AvatarToolPath,
            "-ReportDir", $ReportDir
        )
        if (-not [string]::IsNullOrWhiteSpace($UnityEditorPath)) { $args += @("-UnityEditorPath", $UnityEditorPath) }
        if (-not [string]::IsNullOrWhiteSpace($ReportSuffix)) { $args += @("-ReportSuffix", $ReportSuffix) }
        & powershell @args
    }
}
finally {
    Pop-Location
}

exit 0

