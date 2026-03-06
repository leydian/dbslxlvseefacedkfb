param(
    [string]$PackageRoot = ".\unity\Packages\com.vsfclone.xav2",
    [string]$PublicDocsRoot = ".\docs\public"
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

function Assert-PathExists {
    param(
        [Parameter(Mandatory = $true)][string]$Path,
        [Parameter(Mandatory = $true)][string]$Label
    )

    if (-not (Test-Path -LiteralPath $Path)) {
        throw "[xav2_package_release_gate] missing_required_file label=$Label path=$Path"
    }
}

Assert-PathExists -Path $PackageRoot -Label "package_root"
Assert-PathExists -Path (Join-Path $PackageRoot "package.json") -Label "package_json"
Assert-PathExists -Path (Join-Path $PackageRoot "README.md") -Label "readme"
Assert-PathExists -Path (Join-Path $PackageRoot "LICENSE") -Label "license"
Assert-PathExists -Path (Join-Path $PackageRoot "NOTICE") -Label "notice"
Assert-PathExists -Path (Join-Path $PackageRoot "ThirdPartyNotices.md") -Label "third_party_notices"

Assert-PathExists -Path (Join-Path $PublicDocsRoot "compatibility.md") -Label "compatibility_doc"
Assert-PathExists -Path (Join-Path $PublicDocsRoot "migration.md") -Label "migration_doc"
Assert-PathExists -Path (Join-Path $PublicDocsRoot "error-codes.md") -Label "error_codes_doc"

$packageJsonPath = Join-Path $PackageRoot "package.json"
$pkg = Get-Content -Raw -Path $packageJsonPath | ConvertFrom-Json

if (-not $pkg.documentationUrl) {
    throw "[xav2_package_release_gate] package.json missing documentationUrl"
}
if (-not $pkg.changelogUrl) {
    throw "[xav2_package_release_gate] package.json missing changelogUrl"
}
if (-not $pkg.licensesUrl) {
    throw "[xav2_package_release_gate] package.json missing licensesUrl"
}
if (-not $pkg.repository.url) {
    throw "[xav2_package_release_gate] package.json missing repository.url"
}
if (-not $pkg.samples -or $pkg.samples.Count -lt 2) {
    throw "[xav2_package_release_gate] package.json must define at least two samples"
}

$sampleRoots = @(
    (Join-Path $PackageRoot "Samples~\RuntimeLoadSample"),
    (Join-Path $PackageRoot "Samples~\ExportImportRoundtripSample")
)
foreach ($sampleRoot in $sampleRoots) {
    Assert-PathExists -Path $sampleRoot -Label "sample_root"
}

Write-Host "[xav2_package_release_gate] PASS package=$($pkg.name) version=$($pkg.version)"
exit 0
