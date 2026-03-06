param(
    [Parameter(Mandatory = $true)][string]$InputDir,
    [string]$OutputJson = ".\build\reports\avatar_batch_validation.json",
    [string]$OutputTxt = ".\build\reports\avatar_batch_validation.txt"
)

$ErrorActionPreference = "Stop"

if (-not (Test-Path $InputDir)) {
    throw "InputDir does not exist: $InputDir"
}

$exts = @(".vrm", ".vxavatar", ".vxa2", ".miq", ".vsfavatar")
$files = Get-ChildItem -Path $InputDir -Recurse -File | Where-Object { $exts -contains $_.Extension.ToLowerInvariant() }

$rows = @()
foreach ($f in $files) {
    $rows += [PSCustomObject]@{
        path = $f.FullName
        extension = $f.Extension.ToLowerInvariant()
        exists = $true
        size_bytes = $f.Length
        status = if ($f.Length -gt 0) { "PASS" } else { "FAIL_EMPTY_FILE" }
    }
}

$summary = [PSCustomObject]@{
    generated_utc = (Get-Date).ToUniversalTime().ToString("s")
    input_dir = (Resolve-Path $InputDir).Path
    total = @($rows).Count
    passed = @($rows | Where-Object { $_.status -eq "PASS" }).Count
    failed = @($rows | Where-Object { $_.status -ne "PASS" }).Count
    rows = $rows
}

$outDir = Split-Path -Parent $OutputJson
if (-not (Test-Path $outDir)) {
    New-Item -ItemType Directory -Path $outDir | Out-Null
}

$summary | ConvertTo-Json -Depth 6 | Set-Content -Path $OutputJson

$lines = @()
$lines += "Avatar Batch Validation"
$lines += "GeneratedUTC: $($summary.generated_utc)"
$lines += "InputDir: $($summary.input_dir)"
$lines += "Total: $($summary.total)"
$lines += "Passed: $($summary.passed)"
$lines += "Failed: $($summary.failed)"
$lines += ""
foreach ($r in $rows) {
    $lines += "- [$($r.status)] $($r.extension) $($r.size_bytes)B :: $($r.path)"
}
$lines | Set-Content -Path $OutputTxt

Write-Host "json=$OutputJson"
Write-Host "txt=$OutputTxt"
