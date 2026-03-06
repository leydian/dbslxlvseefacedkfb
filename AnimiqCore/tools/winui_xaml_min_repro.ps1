param(
    [string]$ProjectPath = ".\host\WinUiHost\WinUiHost.csproj",
    [string]$Configuration = "Release",
    [string]$SummaryPath = ".\build\reports\winui_xaml_min_repro_summary.txt",
    [string]$SummaryJsonPath = ".\build\reports\winui_xaml_min_repro_summary.json",
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
$resolvedSummaryJson = Resolve-AbsolutePath -Path $SummaryJsonPath -BaseDirectory $repoRoot
$resolvedDiag = Resolve-AbsolutePath -Path $DiagLogPath -BaseDirectory $repoRoot
$resolvedBinlog = Resolve-AbsolutePath -Path $BinlogPath -BaseDirectory $repoRoot
New-Item -ItemType Directory -Force -Path (Split-Path -Parent $resolvedSummary) | Out-Null
New-Item -ItemType Directory -Force -Path (Split-Path -Parent $resolvedSummaryJson) | Out-Null
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
$failureHints = [System.Collections.Generic.List[string]]::new()
$diagnosticEntries = [System.Collections.Generic.List[object]]::new()
if ($exitCode -ne 0) {
    $failureClass = "UNKNOWN"
    if (Select-String -Path $resolvedDiag -Pattern "WMC9999" -SimpleMatch -Quiet) {
        $failureClass = "TOOLCHAIN_XAML_PLATFORM_UNSUPPORTED"
        $failureHints.Add("WMC9999 detected in diagnostic log.")
    } elseif (Select-String -Path $resolvedDiag -Pattern "Windows SDK version 10.0.19041.0 was not found" -SimpleMatch -Quiet) {
        $failureClass = "TOOLCHAIN_WINDOWS_SDK_INCOMPLETE"
        $failureHints.Add("Required Windows SDK 10.0.19041.0 not found.")
    } elseif (Select-String -Path $resolvedDiag -Pattern "Windows.winmd" -SimpleMatch -Quiet) {
        $failureClass = "TOOLCHAIN_WINDOWS_SDK_INCOMPLETE"
        $failureHints.Add("Windows.winmd resolution failure detected.")
    } elseif (Select-String -Path $resolvedDiag -Pattern "Microsoft.WindowsAppSDK" -SimpleMatch -Quiet) {
        $failureClass = "WINDOWSAPPSDK_RESTORE_INCOMPLETE"
        $failureHints.Add("WindowsAppSDK package resolution issue detected.")
    } elseif (Select-String -Path $resolvedDiag -Pattern "NU1301" -SimpleMatch -Quiet) {
        $failureClass = "NUGET_SOURCE_UNREACHABLE"
        $failureHints.Add("NU1301 detected (NuGet source unreachable).")
    } elseif (Select-String -Path $resolvedDiag -Pattern "NU1101" -SimpleMatch -Quiet) {
        $failureClass = "NUGET_PACKAGE_RESOLUTION_FAIL"
        $failureHints.Add("NU1101 detected (package not found).")
    } elseif (Select-String -Path $resolvedDiag -Pattern "XamlCompiler.exe" -SimpleMatch -Quiet) {
        $failureClass = "XAML_COMPILER_EXEC_FAIL"
        $failureHints.Add("XamlCompiler.exe execution path detected.")
    }
}

$diagLines = Get-Content -Path $resolvedDiag -ErrorAction SilentlyContinue
if ($null -eq $diagLines) {
    $diagLines = @()
}
foreach ($line in $diagLines) {
    if ($line -match '^(?:\d{2}:\d{2}:\d{2}\.\d+\s+\d+>)?\s*(?<file>[A-Za-z]:\\[^:(]+?)\((?<line>\d+),(?<col>\d+)\):\s*(?:.+?\s+)?(?<severity>error|warning)\s+(?<code>[A-Za-z]+\d+)\s*:\s*(?<message>.+)$') {
        $diagnosticEntries.Add([ordered]@{
            file = $matches['file']
            line = [int]$matches['line']
            column = [int]$matches['col']
            severity = $matches['severity'].ToLowerInvariant()
            code = $matches['code']
            message = $matches['message'].Trim()
        })
    }
}
$wmc9999Rows = @($diagnosticEntries | Where-Object { $_.code -eq "WMC9999" })
$wmc9999Count = $wmc9999Rows.Count
if ($wmc9999Rows.Count -eq 0) {
    $wmcFallback = @($diagLines | Where-Object { $_ -match '\bWMC9999\b' })
    if ($wmcFallback.Count -gt 0) {
        $wmc9999Count = $wmcFallback.Count
        $failureHints.Add("WMC9999 detected in diagnostic log (fallback parse).")
    }
}
$firstError = $null
if ($diagnosticEntries.Count -gt 0) {
    $firstError = $diagnosticEntries[0]
}

$lines = [System.Collections.Generic.List[string]]::new()
$lines.Add("WinUI XAML Minimal Repro Summary")
$lines.Add("Generated: $(Get-Date -Format o)")
$lines.Add("ProjectPath: $resolvedProject")
$lines.Add("Configuration: $Configuration")
$lines.Add("NoRestore: $NoRestore")
$lines.Add("ExitCode: $exitCode")
$lines.Add("FailureClass: $failureClass")
$lines.Add("FailureHints: $(if ($failureHints.Count -eq 0) { '<none>' } else { $failureHints -join ' | ' })")
$lines.Add("DiagnosticEntryCount: $($diagnosticEntries.Count)")
$lines.Add("WMC9999Count: $wmc9999Count")
if ($null -ne $firstError) {
    $lines.Add("FirstDiagnostic: $($firstError.file):$($firstError.line):$($firstError.column) [$($firstError.code)] $($firstError.message)")
}
$lines.Add("DiagLog: $resolvedDiag")
$lines.Add("Binlog: $resolvedBinlog")
$lines | Set-Content -Path $resolvedSummary -Encoding UTF8

$json = [ordered]@{
    generated_at_utc = (Get-Date).ToUniversalTime().ToString("o")
    project_path = $resolvedProject
    configuration = $Configuration
    no_restore = [bool]$NoRestore
    exit_code = [int]$exitCode
    failure_class = $failureClass
    failure_hints = @($failureHints)
    diagnostic_entry_count = [int]$diagnosticEntries.Count
    wmc9999_count = [int]$wmc9999Count
    first_diagnostic = $firstError
    diagnostics = @($diagnosticEntries)
    diag_log = $resolvedDiag
    binlog = $resolvedBinlog
}
$json | ConvertTo-Json -Depth 6 | Set-Content -Path $resolvedSummaryJson -Encoding UTF8
Write-Host "summary=$resolvedSummary"
Write-Host "json=$resolvedSummaryJson"

if ($exitCode -ne 0) { exit 1 }
