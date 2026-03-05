param(
    [string]$ProcessName = "vsfavatar_sidecar",
    [string]$BinaryPath = ".\build\Release\vsfavatar_sidecar.exe",
    [string]$SummaryPath = ".\build\reports\sidecar_lock_guard_summary.txt"
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
$resolvedBinary = Resolve-AbsolutePath -Path $BinaryPath -BaseDirectory $repoRoot
$resolvedSummary = Resolve-AbsolutePath -Path $SummaryPath -BaseDirectory $repoRoot

$killed = 0
$errors = [System.Collections.Generic.List[string]]::new()
try {
    $procs = @(Get-Process -Name $ProcessName -ErrorAction SilentlyContinue)
    foreach ($p in $procs) {
        try {
            Stop-Process -Id $p.Id -Force -ErrorAction Stop
            $killed++
        } catch {
            $errors.Add("failed to stop pid=$($p.Id): $($_.Exception.Message)")
        }
    }
} catch {
    $errors.Add("process enumeration failed: $($_.Exception.Message)")
}

$binaryExists = Test-Path $resolvedBinary
$binaryInfo = if ($binaryExists) { Get-Item $resolvedBinary } else { $null }

$lines = [System.Collections.Generic.List[string]]::new()
$lines.Add("Sidecar Lock Guard Summary")
$lines.Add("Generated: $(Get-Date -Format o)")
$lines.Add("ProcessName: $ProcessName")
$lines.Add("BinaryPath: $resolvedBinary")
$lines.Add("BinaryExists: $binaryExists")
if ($binaryExists) {
    $lines.Add("BinaryLength: $($binaryInfo.Length)")
    $lines.Add("BinaryLastWriteUtc: $($binaryInfo.LastWriteTimeUtc.ToString("o"))")
}
$lines.Add("KilledProcessCount: $killed")
$lines.Add("")
$lines.Add("Errors:")
if ($errors.Count -eq 0) {
    $lines.Add("- <none>")
} else {
    foreach ($e in $errors) { $lines.Add("- $e") }
}

New-Item -ItemType Directory -Force -Path (Split-Path -Parent $resolvedSummary) | Out-Null
$lines | Set-Content -Path $resolvedSummary -Encoding UTF8
Write-Host "summary=$resolvedSummary"
