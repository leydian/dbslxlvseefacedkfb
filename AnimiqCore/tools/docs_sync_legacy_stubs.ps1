Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$repoRoot = Split-Path -Parent $PSScriptRoot
$docsRoot = Join-Path $repoRoot "docs"
$reportsRoot = Join-Path $docsRoot "reports"
$weeklyRoot = Join-Path $reportsRoot "weekly"
$archiveRoot = Join-Path $docsRoot "archive/reports-legacy"
$legacyMapPath = Join-Path $reportsRoot "legacy-map.md"

$excludedRootDocs = @(
    "DOMAIN_INDEX.md",
    "legacy-map.md",
    "TEMPLATE.md"
)

function Get-IsoWeekFolder {
    param(
        [Parameter(Mandatory)] [datetime] $Date
    )

    # .NET Framework compatibility (PowerShell 5.1): emulate ISO week/year.
    $day = [int]$Date.DayOfWeek
    if ($day -eq 0) { $day = 7 } # Sunday -> 7
    $thursday = $Date.AddDays(4 - $day)
    $year = $thursday.Year
    $calendar = [System.Globalization.CultureInfo]::InvariantCulture.Calendar
    $week = $calendar.GetWeekOfYear($thursday, [System.Globalization.CalendarWeekRule]::FirstFourDayWeek, [DayOfWeek]::Monday)
    return ("{0}-W{1:d2}" -f $year, $week)
}

function Get-CanonicalPathFromLegacyName {
    param(
        [Parameter(Mandatory)] [string] $LegacyFileName
    )

    $m = [regex]::Match($LegacyFileName, '^(?<prefix>.+)_(?<date>\d{4}-\d{2}-\d{2})\.md$')
    if (-not $m.Success) {
        throw "Legacy file name does not match expected pattern: $LegacyFileName"
    }

    $prefix = $m.Groups["prefix"].Value.ToLowerInvariant()
    $dateText = $m.Groups["date"].Value
    $date = [datetime]::ParseExact($dateText, "yyyy-MM-dd", [System.Globalization.CultureInfo]::InvariantCulture)
    $weekFolder = Get-IsoWeekFolder -Date $date
    $canonicalName = "{0}_{1}.md" -f $dateText, $prefix
    return "docs/reports/weekly/{0}/{1}" -f $weekFolder, $canonicalName
}

function Write-LegacyStub {
    param(
        [Parameter(Mandatory)] [string] $LegacyAbsPath,
        [Parameter(Mandatory)] [string] $NewRelFromReports,
        [Parameter(Mandatory)] [string] $ArchiveRelFromReports
    )

    $content = @(
        "# Legacy Report Redirect",
        "",
        "This document moved after documentation restructuring.",
        "",
        "- New location: [${NewRelFromReports}](${NewRelFromReports})",
        "- Archived original snapshot: [${ArchiveRelFromReports}](${ArchiveRelFromReports})",
        ""
    )
    Set-Content -Path $LegacyAbsPath -Value $content -Encoding UTF8
}

function Get-RelativePath {
    param(
        [Parameter(Mandatory)] [string] $BasePath,
        [Parameter(Mandatory)] [string] $TargetPath
    )

    $baseUri = [System.Uri]((Resolve-Path $BasePath).Path + [System.IO.Path]::DirectorySeparatorChar)
    $targetUri = [System.Uri](Resolve-Path $TargetPath).Path
    $relative = $baseUri.MakeRelativeUri($targetUri).ToString()
    return $relative -replace '/', '\'
}

$rootReportFiles = @(Get-ChildItem -Path $reportsRoot -File -Filter "*.md" | Where-Object {
    $excludedRootDocs -notcontains $_.Name -and $_.Name -match '^.+_\d{4}-\d{2}-\d{2}\.md$'
})

$mapRows = @()

foreach ($legacy in $rootReportFiles) {
    $legacyAbsPath = $legacy.FullName
    $legacyRel = "docs/reports/{0}" -f $legacy.Name
    $legacyRaw = Get-Content -Path $legacyAbsPath -Raw -Encoding UTF8

    $newRel = $null
    $archiveRel = $null

    $newMatch = [regex]::Match($legacyRaw, '\-\s*New location:\s*\[[^\]]+\]\(([^)]+)\)')
    $archiveMatch = [regex]::Match($legacyRaw, '\-\s*Archived original snapshot:\s*\[[^\]]+\]\(([^)]+)\)')

    if ($newMatch.Success -and $archiveMatch.Success) {
        $newRel = "docs/reports/{0}" -f (($newMatch.Groups[1].Value.Trim() -replace '^\.\/', '') -replace '\\', '/')
        $archiveRel = "docs/{0}" -f (($archiveMatch.Groups[1].Value.Trim() -replace '^\.\./', '') -replace '\\', '/')
    }
    else {
        $newRel = Get-CanonicalPathFromLegacyName -LegacyFileName $legacy.Name
        $archiveRel = "docs/archive/reports-legacy/{0}" -f $legacy.Name
    }

    $newAbs = Join-Path $repoRoot $newRel
    $archiveAbs = Join-Path $repoRoot $archiveRel

    if (-not (Test-Path $newAbs)) {
        New-Item -Path (Split-Path -Parent $newAbs) -ItemType Directory -Force | Out-Null
        Copy-Item -Path $legacyAbsPath -Destination $newAbs -Force
    }

    if (-not (Test-Path $archiveAbs)) {
        New-Item -Path (Split-Path -Parent $archiveAbs) -ItemType Directory -Force | Out-Null
        if ($newMatch.Success -and $archiveMatch.Success) {
            Copy-Item -Path $newAbs -Destination $archiveAbs -Force
        }
        else {
            Copy-Item -Path $legacyAbsPath -Destination $archiveAbs -Force
        }
    }

    $newFromReports = ".\{0}" -f (Get-RelativePath -BasePath $reportsRoot -TargetPath $newAbs)
    $archiveFromReports = "..\{0}" -f (Get-RelativePath -BasePath $docsRoot -TargetPath $archiveAbs)
    $newFromReports = $newFromReports -replace '\\', '/'
    $archiveFromReports = $archiveFromReports -replace '\\', '/'

    Write-LegacyStub -LegacyAbsPath $legacyAbsPath -NewRelFromReports $newFromReports -ArchiveRelFromReports $archiveFromReports

    $mapRows += [pscustomobject]@{
        OldPath = $legacyRel -replace '\\', '/'
        NewPath = $newRel -replace '\\', '/'
        ArchivePath = $archiveRel -replace '\\', '/'
    }
}

$mapRows = $mapRows | Sort-Object OldPath -Unique

$mapLines = @(
    "# Legacy Report Path Map",
    "",
    "Old path to new path mapping after documentation restructuring.",
    "",
    "| Old Path | New Path | Archive Snapshot |",
    "|---|---|---|"
)

foreach ($row in $mapRows) {
    $mapLines += "| ``{0}`` | ``{1}`` | ``{2}`` |" -f $row.OldPath, $row.NewPath, $row.ArchivePath
}

Set-Content -Path $legacyMapPath -Value ($mapLines -replace '``', '`') -Encoding UTF8
Write-Host ("Synced legacy stubs and rebuilt legacy-map.md with {0} rows." -f $mapRows.Count)
