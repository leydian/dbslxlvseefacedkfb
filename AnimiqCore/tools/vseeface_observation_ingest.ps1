param(
    [Parameter(Mandatory = $true)][string]$InputDir,
    [string]$EngineBuild = "1.13.38c2",
    [string]$OutputJson = ".\build\reports\vseeface_observations.generated.json"
)

$ErrorActionPreference = "Stop"

if (-not (Test-Path $InputDir)) {
    throw "InputDir not found: $InputDir"
}

$files = Get-ChildItem -Path $InputDir -File -Filter *.json | Sort-Object Name
if ($files.Count -eq 0) {
    throw "no json files under: $InputDir"
}

$rows = [System.Collections.Generic.List[object]]::new()
foreach ($f in $files) {
    $o = Get-Content -Path $f.FullName -Raw | ConvertFrom-Json
    $id = if ($null -ne $o.id -and -not [string]::IsNullOrWhiteSpace("$($o.id)")) { "$($o.id)" } else { [System.IO.Path]::GetFileNameWithoutExtension($f.Name) }
    $rows.Add([PSCustomObject]@{
        id = $id
        load_ok = [bool]$o.load_ok
        render_visible = [bool]$o.render_visible
        crash_or_freeze = [bool]$o.crash_or_freeze
        elapsed_ms = if ($null -ne $o.elapsed_ms) { [int]$o.elapsed_ms } else { $null }
        runtime_ready = [bool](([bool]$o.load_ok) -and -not ([bool]$o.crash_or_freeze))
        note = if ($null -ne $o.note) { "$($o.note)" } else { "auto-ingested" }
        source_file = $f.Name
    })
}

$summary = [PSCustomObject]@{
    version = "1.1"
    engine = [PSCustomObject]@{
        name = "VSeeFace"
        build = $EngineBuild
    }
    generated_utc = (Get-Date).ToUniversalTime().ToString("o")
    input_dir = (Resolve-Path $InputDir).Path
    total_rows = $rows.Count
    rows = $rows
}

$outAbs = if ([System.IO.Path]::IsPathRooted($OutputJson)) { $OutputJson } else { [System.IO.Path]::GetFullPath((Join-Path (Get-Location) $OutputJson)) }
$outDir = Split-Path -Parent $outAbs
if (-not (Test-Path $outDir)) {
    New-Item -ItemType Directory -Path $outDir | Out-Null
}

$summary | ConvertTo-Json -Depth 8 | Set-Content -Path $outAbs -Encoding UTF8
Write-Host "json=$outAbs"
