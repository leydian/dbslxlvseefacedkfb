Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$repoRoot = Split-Path -Parent $PSScriptRoot
$weeklyRoot = Join-Path $repoRoot "docs/reports/weekly"

if (-not (Test-Path $weeklyRoot)) {
    throw "Weekly reports root not found: $weeklyRoot"
}

$weekDirs = @(Get-ChildItem -Path $weeklyRoot -Directory | Sort-Object Name)

foreach ($weekDir in $weekDirs) {
    $reportFiles = @(Get-ChildItem -Path $weekDir.FullName -File -Filter "*.md" | Where-Object {
        $_.Name -ne "INDEX.md" -and $_.Name -ne "SUMMARY.md"
    } | Sort-Object Name)

    $lines = @(
        "# Weekly Reports $($weekDir.Name)",
        "",
        "Report list for this week.",
        "",
        "## Reports"
    )

    foreach ($report in $reportFiles) {
        $lines += "- [$($report.Name)](./$($report.Name))"
    }

    Set-Content -Path (Join-Path $weekDir.FullName "INDEX.md") -Value $lines -Encoding UTF8
}

$weeklyIndexLines = @(
    "# Weekly Reports Index",
    "",
    "Weekly report navigation hub.",
    ""
)

foreach ($weekDir in ($weekDirs | Sort-Object Name -Descending)) {
    $count = @(Get-ChildItem -Path $weekDir.FullName -File -Filter "*.md" | Where-Object {
        $_.Name -ne "INDEX.md" -and $_.Name -ne "SUMMARY.md"
    }).Count
    $weeklyIndexLines += "- [$($weekDir.Name)](./$($weekDir.Name)/INDEX.md) ($count reports)"
}

Set-Content -Path (Join-Path $weeklyRoot "INDEX.md") -Value $weeklyIndexLines -Encoding UTF8
Write-Host ("Rebuilt weekly indexes for {0} week folder(s)." -f $weekDirs.Count)
