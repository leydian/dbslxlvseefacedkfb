param(
    [Parameter(Mandatory = $true)][string]$VSeeFaceRoot,
    [string]$OutputJson = ".\build\reports\vseeface_managed_probe.json",
    [string]$OutputTxt = ".\build\reports\vseeface_managed_probe.txt"
)

$ErrorActionPreference = "Stop"

if (-not (Test-Path $VSeeFaceRoot)) {
    throw "VSeeFaceRoot not found: $VSeeFaceRoot"
}

$managedDir = Join-Path $VSeeFaceRoot "VSeeFace_Data\Managed"
if (-not (Test-Path $managedDir)) {
    throw "Managed directory not found: $managedDir"
}

$dlls = Get-ChildItem -Path $managedDir -File -Filter "*.dll" | Sort-Object Name
$rows = @()
foreach ($dll in $dlls) {
    $name = [System.IO.Path]::GetFileNameWithoutExtension($dll.Name)
    $hash = (Get-FileHash -Path $dll.FullName -Algorithm SHA256).Hash
    $version = $dll.VersionInfo.FileVersion
    $productVersion = $dll.VersionInfo.ProductVersion
    $rows += [PSCustomObject]@{
        name = $name
        file = $dll.Name
        size_bytes = $dll.Length
        file_version = if ([string]::IsNullOrWhiteSpace($version)) { "0.0.0.0" } else { $version }
        product_version = if ([string]::IsNullOrWhiteSpace($productVersion)) { "0.0.0.0" } else { $productVersion }
        sha256 = $hash
    }
}

$summary = [PSCustomObject]@{
    generated_utc = (Get-Date).ToUniversalTime().ToString("o")
    vseeface_root = (Resolve-Path $VSeeFaceRoot).Path
    managed_dir = (Resolve-Path $managedDir).Path
    total_dll = $rows.Count
    has_vrm = @($rows | Where-Object { $_.name -eq "VRM" }).Count -gt 0
    has_unigltf = @($rows | Where-Object { $_.name -eq "UniGLTF" }).Count -gt 0
    has_unihumanoid = @($rows | Where-Object { $_.name -eq "UniHumanoid" }).Count -gt 0
    has_mtoon = @($rows | Where-Object { $_.name -eq "MToon" }).Count -gt 0
    rows = $rows
}

$outDir = Split-Path -Parent $OutputJson
if (-not (Test-Path $outDir)) {
    New-Item -ItemType Directory -Path $outDir | Out-Null
}

$summary | ConvertTo-Json -Depth 8 | Set-Content -Path $OutputJson -Encoding UTF8

$lines = @()
$lines += "VSeeFace Managed Probe"
$lines += "GeneratedUTC: $($summary.generated_utc)"
$lines += "Root: $($summary.vseeface_root)"
$lines += "ManagedDir: $($summary.managed_dir)"
$lines += "TotalDll: $($summary.total_dll)"
$lines += "VRM/UniGLTF/UniHumanoid/MToon: $($summary.has_vrm)/$($summary.has_unigltf)/$($summary.has_unihumanoid)/$($summary.has_mtoon)"
$lines += ""
foreach ($row in $rows) {
    $lines += "- $($row.file) size=$($row.size_bytes) fileVer=$($row.file_version) sha256=$($row.sha256)"
}
$lines | Set-Content -Path $OutputTxt -Encoding UTF8

Write-Host "json=$OutputJson"
Write-Host "txt=$OutputTxt"
