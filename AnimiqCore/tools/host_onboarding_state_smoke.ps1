param(
    [string]$WpfXamlPath = ".\host\WpfHost\MainWindow.xaml",
    [string]$WpfCodeBehindPath = ".\host\WpfHost\MainWindow.xaml.cs",
    [string]$WinUiXamlPath = ".\host\WinUiHost\MainWindow.xaml",
    [string]$WinUiCodeBehindPath = ".\host\WinUiHost\MainWindow.xaml.cs",
    [string]$OutputPath = ".\build\reports\host_onboarding_state_smoke_summary.txt"
)

$ErrorActionPreference = "Stop"

function Resolve-AbsolutePath {
    param([string]$Path, [string]$BaseDirectory)
    if ([System.IO.Path]::IsPathRooted($Path)) {
        return [System.IO.Path]::GetFullPath($Path)
    }
    return [System.IO.Path]::GetFullPath((Join-Path $BaseDirectory $Path))
}

function Has-Pattern {
    param([string]$Path, [string]$Pattern)
    if (-not (Test-Path -LiteralPath $Path)) { return $false }
    return [bool](Select-String -Path $Path -Pattern $Pattern -SimpleMatch -Quiet)
}

$repoRoot = Split-Path -Parent $PSScriptRoot
$wpfXaml = Resolve-AbsolutePath -Path $WpfXamlPath -BaseDirectory $repoRoot
$wpfCode = Resolve-AbsolutePath -Path $WpfCodeBehindPath -BaseDirectory $repoRoot
$winUiXaml = Resolve-AbsolutePath -Path $WinUiXamlPath -BaseDirectory $repoRoot
$winUiCode = Resolve-AbsolutePath -Path $WinUiCodeBehindPath -BaseDirectory $repoRoot
$output = Resolve-AbsolutePath -Path $OutputPath -BaseDirectory $repoRoot
New-Item -ItemType Directory -Force -Path (Split-Path -Parent $output) | Out-Null

$checks = [System.Collections.Generic.List[object]]::new()
$checks.Add([ordered]@{
    name = "wpf_actionability_binding_present"
    pass = (Has-Pattern -Path $wpfXaml -Pattern "ActionabilityBadgeText")
})
$checks.Add([ordered]@{
    name = "wpf_blocked_branch_present"
    pass = (Has-Pattern -Path $wpfCode -Pattern "HostActionability.Blocked")
})
$checks.Add([ordered]@{
    name = "wpf_actionability_logic_present"
    pass = (Has-Pattern -Path $wpfCode -Pattern "HostActionability")
})
$checks.Add([ordered]@{
    name = "winui_ready_text"
    pass = (Has-Pattern -Path $winUiXaml -Pattern "READY")
})
$checks.Add([ordered]@{
    name = "winui_blocked_text"
    pass = (Has-Pattern -Path $winUiXaml -Pattern "BLOCKED")
})
$checks.Add([ordered]@{
    name = "winui_badge_ready_token"
    pass = (Has-Pattern -Path $winUiXaml -Pattern "Brush.BadgeReadyBg")
})
$checks.Add([ordered]@{
    name = "winui_badge_blocked_token"
    pass = (Has-Pattern -Path $winUiXaml -Pattern "Brush.BadgeBlockedBg")
})
$checks.Add([ordered]@{
    name = "winui_code_uses_resource_lookup"
    pass = (Has-Pattern -Path $winUiCode -Pattern "ResolveBrush(")
})

$overall = @($checks | Where-Object { -not $_.pass }).Count -eq 0
$lines = @()
$lines += "Host Onboarding State Smoke Summary"
$lines += "Generated: $(Get-Date -Format o)"
$lines += "Overall: $(if ($overall) { 'PASS' } else { 'FAIL' })"
$lines += "WpfXamlPath: $wpfXaml"
$lines += "WpfCodeBehindPath: $wpfCode"
$lines += "WinUiXamlPath: $winUiXaml"
$lines += "WinUiCodeBehindPath: $winUiCode"
$lines += ""
$lines += "Checks:"
foreach ($c in $checks) {
    $lines += "- $($c.name): $(if ($c.pass) { 'PASS' } else { 'FAIL' })"
}
$lines | Set-Content -Path $output -Encoding UTF8
Write-Host "summary=$output"
if (-not $overall) {
    exit 1
}
