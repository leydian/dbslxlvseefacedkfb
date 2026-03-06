param(
    [Parameter(Mandatory = $true)][string]$InputDir,
    [string]$AvatarToolPath = ".\build\Release\avatar_tool.exe",
    [string]$OutputJson = ".\build\reports\avatar_batch_load_validation.json",
    [string]$OutputTxt = ".\build\reports\avatar_batch_load_validation.txt",
    [string[]]$IncludeExtensions = @(".vrm", ".miq", ".vsfavatar"),
    [switch]$AllowPartialCompat,
    [switch]$AllowFailedCompat,
    [switch]$AllowNonRuntimeReady,
    [switch]$AllowPrimaryError
)

$ErrorActionPreference = "Stop"

if (-not (Test-Path $InputDir)) {
    throw "InputDir does not exist: $InputDir"
}
if (-not (Test-Path $AvatarToolPath)) {
    throw "AvatarToolPath does not exist: $AvatarToolPath"
}

$normalizedExts = @($IncludeExtensions | ForEach-Object { "$_".ToLowerInvariant() })
$files = Get-ChildItem -Path $InputDir -Recurse -File | Where-Object {
    $normalizedExts -contains $_.Extension.ToLowerInvariant()
}

$rows = @()
foreach ($f in $files) {
    $raw = & $AvatarToolPath $f.FullName 2>&1
    $exitCode = $LASTEXITCODE
    $outputText = ($raw -join "`n")
    $fields = @{}
    foreach ($line in $raw) {
        if ($line -match '^\s*([^:]+):\s*(.*)$') {
            $fields[$matches[1].Trim()] = $matches[2].Trim()
        }
    }

    $loadSucceeded = ($exitCode -eq 0) -and ($outputText -match "Load succeeded")
    $compat = "$($fields["Compat"])"
    $parserStage = "$($fields["ParserStage"])"
    $primaryError = "$($fields["PrimaryError"])"
    $criticalWarningCount = 0
    if ($fields.ContainsKey("CriticalWarningCount")) {
        [void][int]::TryParse("$($fields["CriticalWarningCount"])", [ref]$criticalWarningCount)
    }

    $compatOk = $compat -eq "full" -or ($AllowPartialCompat.IsPresent -and $compat -eq "partial") -or ($AllowFailedCompat.IsPresent -and $compat -eq "failed")
    $stageOk = $parserStage -eq "runtime-ready" -or $AllowNonRuntimeReady.IsPresent
    $primaryOk = $primaryError -eq "NONE" -or $AllowPrimaryError.IsPresent
    $status = if ($loadSucceeded -and $compatOk -and $stageOk -and $primaryOk) { "PASS" } else { "FAIL" }

    $rows += [PSCustomObject]@{
        path = $f.FullName
        extension = $f.Extension.ToLowerInvariant()
        status = $status
        load_succeeded = $loadSucceeded
        exit_code = $exitCode
        compat = $compat
        parser_stage = $parserStage
        primary_error = $primaryError
        critical_warning_count = $criticalWarningCount
        first_line = if ($raw.Count -gt 0) { "$($raw[0])" } else { "" }
    }
}

$summary = [PSCustomObject]@{
    generated_utc = (Get-Date).ToUniversalTime().ToString("s")
    input_dir = (Resolve-Path $InputDir).Path
    avatar_tool_path = (Resolve-Path $AvatarToolPath).Path
    total = @($rows).Count
    passed = @($rows | Where-Object { $_.status -eq "PASS" }).Count
    failed = @($rows | Where-Object { $_.status -ne "PASS" }).Count
    policy = [PSCustomObject]@{
        allow_partial_compat = $AllowPartialCompat.IsPresent
        allow_failed_compat = $AllowFailedCompat.IsPresent
        allow_non_runtime_ready = $AllowNonRuntimeReady.IsPresent
        allow_primary_error = $AllowPrimaryError.IsPresent
    }
    rows = $rows
}

$outDir = Split-Path -Parent $OutputJson
if (-not (Test-Path $outDir)) {
    New-Item -ItemType Directory -Path $outDir | Out-Null
}

$summary | ConvertTo-Json -Depth 8 | Set-Content -Path $OutputJson -Encoding UTF8

$lines = @()
$lines += "Avatar Batch Load Validation"
$lines += "GeneratedUTC: $($summary.generated_utc)"
$lines += "InputDir: $($summary.input_dir)"
$lines += "AvatarToolPath: $($summary.avatar_tool_path)"
$lines += "Total: $($summary.total)"
$lines += "Passed: $($summary.passed)"
$lines += "Failed: $($summary.failed)"
$lines += "Policy: partial=$($summary.policy.allow_partial_compat), failed=$($summary.policy.allow_failed_compat), non_runtime_ready=$($summary.policy.allow_non_runtime_ready), primary_error=$($summary.policy.allow_primary_error)"
$lines += ""
foreach ($r in $rows) {
    $lines += "- [$($r.status)] $($r.extension) :: $($r.path)"
    $lines += "  load=$($r.load_succeeded), exit=$($r.exit_code), compat=$($r.compat), stage=$($r.parser_stage), primary=$($r.primary_error), critical=$($r.critical_warning_count)"
}
$lines | Set-Content -Path $OutputTxt -Encoding UTF8

Write-Host "json=$OutputJson"
Write-Host "txt=$OutputTxt"

if ($summary.failed -gt 0) {
    exit 1
}
