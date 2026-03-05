param(
    [string]$ProjectPath = ".\host\WinUiHost\WinUiHost.csproj",
    [string]$Configuration = "Release",
    [string]$SummaryPath = ".\build\reports\winui_xaml_min_repro_summary.txt",
    [string]$DiagLogPath = ".\build\reports\winui\winui_min_repro_diag.log",
    [string]$BinlogPath = ".\build\reports\winui\winui_min_repro.binlog",
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
$resolvedDiag = Resolve-AbsolutePath -Path $DiagLogPath -BaseDirectory $repoRoot
$resolvedBinlog = Resolve-AbsolutePath -Path $BinlogPath -BaseDirectory $repoRoot
New-Item -ItemType Directory -Force -Path (Split-Path -Parent $resolvedSummary) | Out-Null
New-Item -ItemType Directory -Force -Path (Split-Path -Parent $resolvedDiag) | Out-Null

if (-not (Test-Path $resolvedProject)) {
    throw "project not found: $resolvedProject"
}

$args = @(
    "build",
    $resolvedProject,
    "-c", $Configuration,
    "-p:Platform=x64",
    "-p:UseXamlCompilerExecutable=false",
    "-v:diag",
    "-bl:$resolvedBinlog"
)
if ($NoRestore) { $args += "--no-restore" }

Push-Location $repoRoot
try {
    & dotnet @args 1> $resolvedDiag 2>&1
    $exitCode = $LASTEXITCODE
} finally {
    Pop-Location
}

$failureClass = "NONE"
if ($exitCode -ne 0) {
    $failureClass = "UNKNOWN"
    if (Select-String -Path $resolvedDiag -Pattern "WMC9999" -SimpleMatch -Quiet) {
        $failureClass = "TOOLCHAIN_XAML_PLATFORM_UNSUPPORTED"
    } elseif (Select-String -Path $resolvedDiag -Pattern "NU1301" -SimpleMatch -Quiet) {
        $failureClass = "NUGET_SOURCE_UNREACHABLE"
    } elseif (Select-String -Path $resolvedDiag -Pattern "NU1101" -SimpleMatch -Quiet) {
        $failureClass = "NUGET_PACKAGE_RESOLUTION_FAIL"
    } elseif (Select-String -Path $resolvedDiag -Pattern "XamlCompiler.exe" -SimpleMatch -Quiet) {
        $failureClass = "XAML_COMPILER_EXEC_FAIL"
    }
}

$lines = [System.Collections.Generic.List[string]]::new()
$lines.Add("WinUI XAML Minimal Repro Summary")
$lines.Add("Generated: $(Get-Date -Format o)")
$lines.Add("ProjectPath: $resolvedProject")
$lines.Add("Configuration: $Configuration")
$lines.Add("NoRestore: $NoRestore")
$lines.Add("ExitCode: $exitCode")
$lines.Add("FailureClass: $failureClass")
$lines.Add("DiagLog: $resolvedDiag")
$lines.Add("Binlog: $resolvedBinlog")
$lines | Set-Content -Path $resolvedSummary -Encoding UTF8
Write-Host "summary=$resolvedSummary"

if ($exitCode -ne 0) { exit 1 }
