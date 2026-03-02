param(
    [string]$SampleDir = "..\\sample",
    [string]$AvatarToolPath = ".\\build\\Release\\avatar_tool.exe",
    [string]$VrmToXav2Path = ".\\build\\Release\\vrm_to_xav2.exe",
    [string]$OutputPath = ".\\build\\reports\\vxavatar_probe_latest.txt",
    [ValidateSet("quick", "full")][string]$Profile = "quick",
    [int]$MaxFiles = 20,
    [int]$FullMaxFiles = 200,
    [int]$FixedXav2FromVrmCount = 5,
    [string[]]$FixedXav2FromVrmAllowlist = @(
        "Kikyo_FT Variant.vrm",
        "Kikyo_FT Variant111.vrm",
        "Kikyo_FT Variant112.vrm",
        "MANUKA_FT Variant(Clone).vrm",
        "NewOnYou.vrm"
    ),
    [switch]$UseFixedSet,
    [string[]]$FixedVxSamples = @(
        "demo_mvp.vxavatar"
    ),
    [string[]]$FixedVxa2Samples = @(
        "demo_mvp.vxa2"
    ),
    [string[]]$FixedXav2Samples = @(
        "demo_mvp.xav2"
    )
)

$ErrorActionPreference = "Stop"

function Find-SignatureOffset {
    param(
        [byte[]]$Buffer,
        [byte[]]$Signature
    )

    for ($i = 0; $i -le $Buffer.Length - $Signature.Length; $i++) {
        $match = $true
        for ($j = 0; $j -lt $Signature.Length; $j++) {
            if ($Buffer[$i + $j] -ne $Signature[$j]) {
                $match = $false
                break
            }
        }
        if ($match) {
            return $i
        }
    }

    return -1
}

function Write-UInt32LE {
    param(
        [byte[]]$Buffer,
        [int]$Offset,
        [UInt32]$Value
    )

    $bytes = [System.BitConverter]::GetBytes($Value)
    if (-not [System.BitConverter]::IsLittleEndian) {
        [Array]::Reverse($bytes)
    }
    for ($k = 0; $k -lt 4; $k++) {
        $Buffer[$Offset + $k] = $bytes[$k]
    }
}

function Write-UInt16LE {
    param(
        [byte[]]$Buffer,
        [int]$Offset,
        [UInt16]$Value
    )

    $bytes = [System.BitConverter]::GetBytes($Value)
    if (-not [System.BitConverter]::IsLittleEndian) {
        [Array]::Reverse($bytes)
    }
    for ($k = 0; $k -lt 2; $k++) {
        $Buffer[$Offset + $k] = $bytes[$k]
    }
}

function Build-SyntheticFiles {
    param(
        [string]$TmpDir,
        [string]$BaseVxPath,
        [string]$BaseVxa2Path,
        [string]$BaseXav2Path
    )

    if (-not (Test-Path $TmpDir)) {
        New-Item -ItemType Directory -Path $TmpDir | Out-Null
    }

    $out = @{}

    $vxTruncatedPath = Join-Path $TmpDir "demo_mvp_truncated.vxavatar"
    $vxMismatchPath = Join-Path $TmpDir "demo_mvp_cd_mismatch.vxavatar"
    $vxa2TruncatedPath = Join-Path $TmpDir "demo_tlv_truncated.vxa2"
    $xav2ManifestMismatchPath = Join-Path $TmpDir "demo_manifest_mismatch.xav2"
    $xav2TruncatedPath = Join-Path $TmpDir "demo_tlv_truncated.xav2"

    $vxBytes = [System.IO.File]::ReadAllBytes($BaseVxPath)
    if ($vxBytes.Length -lt 32) {
        throw "base vxavatar is too small: $BaseVxPath"
    }

    $truncLength = [Math]::Max(32, $vxBytes.Length - 40)
    [System.IO.File]::WriteAllBytes($vxTruncatedPath, $vxBytes[0..($truncLength - 1)])

    $mismatch = New-Object byte[] $vxBytes.Length
    [Array]::Copy($vxBytes, $mismatch, $vxBytes.Length)
    $localSig = [byte[]](0x50, 0x4B, 0x03, 0x04)
    $localOffset = Find-SignatureOffset -Buffer $mismatch -Signature $localSig
    if ($localOffset -lt 0) {
        throw "local ZIP header signature not found in $BaseVxPath"
    }
    if ($localOffset + 22 -ge $mismatch.Length) {
        throw "local ZIP header is truncated in $BaseVxPath"
    }

    Write-UInt32LE -Buffer $mismatch -Offset ($localOffset + 18) -Value ([UInt32]0x7FFFFFF0)

    $cdSig = [byte[]](0x50, 0x4B, 0x01, 0x02)
    $cdOffset = Find-SignatureOffset -Buffer $mismatch -Signature $cdSig
    if ($cdOffset -lt 0) {
        throw "central directory signature not found in $BaseVxPath"
    }
    if ($cdOffset + 11 -ge $mismatch.Length) {
        throw "central directory header is truncated in $BaseVxPath"
    }
    Write-UInt16LE -Buffer $mismatch -Offset ($cdOffset + 10) -Value ([UInt16]99)

    [System.IO.File]::WriteAllBytes($vxMismatchPath, $mismatch)

    $vxa2Bytes = [System.IO.File]::ReadAllBytes($BaseVxa2Path)
    if ($vxa2Bytes.Length -lt 16) {
        throw "base vxa2 is too small: $BaseVxa2Path"
    }
    $vxa2TruncLength = [Math]::Max(16, $vxa2Bytes.Length - 5)
    [System.IO.File]::WriteAllBytes($vxa2TruncatedPath, $vxa2Bytes[0..($vxa2TruncLength - 1)])

    $xav2Bytes = [System.IO.File]::ReadAllBytes($BaseXav2Path)
    if ($xav2Bytes.Length -lt 16) {
        throw "base xav2 is too small: $BaseXav2Path"
    }

    $manifestMismatch = New-Object byte[] $xav2Bytes.Length
    [Array]::Copy($xav2Bytes, $manifestMismatch, $xav2Bytes.Length)
    Write-UInt32LE -Buffer $manifestMismatch -Offset 6 -Value ([UInt32]0x7FFFFFF0)
    [System.IO.File]::WriteAllBytes($xav2ManifestMismatchPath, $manifestMismatch)

    $xav2TruncLength = [Math]::Max(16, $xav2Bytes.Length - 3)
    [System.IO.File]::WriteAllBytes($xav2TruncatedPath, $xav2Bytes[0..($xav2TruncLength - 1)])

    $out["vx_truncated"] = $vxTruncatedPath
    $out["vx_mismatch"] = $vxMismatchPath
    $out["vxa2_truncated"] = $vxa2TruncatedPath
    $out["xav2_manifest_mismatch"] = $xav2ManifestMismatchPath
    $out["xav2_truncated"] = $xav2TruncatedPath
    return $out
}

function Add-Entry {
    param(
        [System.Collections.Generic.List[object]]$Entries,
        [hashtable]$SeenPaths,
        [string]$Name,
        [string]$Path,
        [string]$Kind,
        [string]$Tag
    )
    $fullPath = [System.IO.Path]::GetFullPath($Path).ToLowerInvariant()
    if ($SeenPaths.ContainsKey($fullPath)) {
        return
    }
    $SeenPaths[$fullPath] = $true
    $Entries.Add([PSCustomObject]@{
        Name = $Name
        Path = [System.IO.Path]::GetFullPath($Path)
        Kind = $Kind
        Tag = $Tag
    })
}

function Add-GeneratedXav2FromVrm {
    param(
        [string]$SampleDir,
        [string]$OutputPath,
        [string]$VrmToXav2Path,
        [int]$Count,
        [string[]]$Allowlist,
        [bool]$FailOnAllowlistMiss,
        [System.Collections.Generic.List[object]]$Entries,
        [hashtable]$SeenPaths,
        [ref]$BaseXav2Path,
        [ref]$BaseVrmPath
    )

    if ($Count -le 0) {
        return
    }
    if (-not (Test-Path $VrmToXav2Path)) {
        return
    }

    $vrmFiles = @()
    if ($Allowlist -and $Allowlist.Count -gt 0) {
        $missing = @()
        foreach ($name in $Allowlist) {
            $p = Join-Path $SampleDir $name
            if (Test-Path $p) {
                $vrmFiles += Get-Item $p
            } else {
                $missing += $name
            }
        }
        if ($FailOnAllowlistMiss -and $missing.Count -gt 0) {
            throw ("missing fixed XAV2 VRM allowlist files: " + ($missing -join ", "))
        }
    }
    if ($vrmFiles.Count -eq 0) {
        $vrmFiles = Get-ChildItem -Path $SampleDir -Filter *.vrm | Select-Object -First $Count
    } else {
        $vrmFiles = $vrmFiles | Select-Object -First $Count
    }
    if ($vrmFiles.Count -eq 0) {
        return
    }

    $tmpDir = Join-Path (Split-Path -Parent $OutputPath) "..\\tmp_vx\\generated_xav2"
    if (-not (Test-Path $tmpDir)) {
        New-Item -ItemType Directory -Path $tmpDir | Out-Null
    }

    foreach ($vrm in $vrmFiles) {
        if ($null -eq $BaseVrmPath.Value) {
            $BaseVrmPath.Value = $vrm.FullName
        }
        $nameNoExt = [System.IO.Path]::GetFileNameWithoutExtension($vrm.Name)
        $generatedXav2Path = Join-Path $tmpDir ("$nameNoExt.xav2")
        & $VrmToXav2Path $vrm.FullName $generatedXav2Path | Out-Null
        if (-not (Test-Path $generatedXav2Path)) {
            continue
        }
        $resolvedGeneratedPath = (Resolve-Path $generatedXav2Path).Path
        if ($null -eq $BaseXav2Path.Value) {
            $BaseXav2Path.Value = $resolvedGeneratedPath
        }
        Add-Entry -Entries $Entries -SeenPaths $SeenPaths -Name ([System.IO.Path]::GetFileName($resolvedGeneratedPath)) -Path $resolvedGeneratedPath -Kind "XAV2" -Tag "fixed-valid"
    }
}

if (-not (Test-Path $AvatarToolPath)) {
    throw "avatar_tool not found at $AvatarToolPath"
}
if (-not (Test-Path $SampleDir)) {
    throw "sample directory not found at $SampleDir"
}

$outDir = Split-Path -Parent $OutputPath
if (-not (Test-Path $outDir)) {
    New-Item -ItemType Directory -Path $outDir | Out-Null
}

$entries = New-Object System.Collections.Generic.List[object]
$seenPaths = @{}
$baseVxPath = $null
$baseVxa2Path = $null
$baseXav2Path = $null
$baseVrmPath = $null
$isFullProfile = $Profile -eq "full"
$discoverLimit = if ($isFullProfile) { $FullMaxFiles } else { $MaxFiles }

if ($UseFixedSet -or $isFullProfile) {
    foreach ($name in $FixedVxSamples) {
        $p = Join-Path $SampleDir $name
        if (Test-Path $p) {
            $rp = (Resolve-Path $p).Path
            Add-Entry -Entries $entries -SeenPaths $seenPaths -Name ([System.IO.Path]::GetFileName($rp)) -Path $rp -Kind "VXAvatar" -Tag "fixed-valid"
            if ($null -eq $baseVxPath) { $baseVxPath = $rp }
        }
    }

    foreach ($name in $FixedVxa2Samples) {
        $p = Join-Path $SampleDir $name
        if (Test-Path $p) {
            $rp = (Resolve-Path $p).Path
            Add-Entry -Entries $entries -SeenPaths $seenPaths -Name ([System.IO.Path]::GetFileName($rp)) -Path $rp -Kind "VXA2" -Tag "fixed-valid"
            if ($null -eq $baseVxa2Path) { $baseVxa2Path = $rp }
        }
    }

    foreach ($name in $FixedXav2Samples) {
        $p = Join-Path $SampleDir $name
        if (Test-Path $p) {
            $rp = (Resolve-Path $p).Path
            Add-Entry -Entries $entries -SeenPaths $seenPaths -Name ([System.IO.Path]::GetFileName($rp)) -Path $rp -Kind "XAV2" -Tag "fixed-valid"
            if ($null -eq $baseXav2Path) { $baseXav2Path = $rp }
        }
    }
}

if ((-not $UseFixedSet) -or $isFullProfile) {
    $vxFiles = Get-ChildItem -Path $SampleDir -Filter *.vxavatar | Select-Object -First $discoverLimit
    foreach ($f in $vxFiles) {
        if ($null -eq $baseVxPath) {
            $baseVxPath = $f.FullName
        }
        $tag = if ($isFullProfile) { "real-full" } else { "discovered" }
        Add-Entry -Entries $entries -SeenPaths $seenPaths -Name $f.Name -Path $f.FullName -Kind "VXAvatar" -Tag $tag
    }

    $vxa2Files = Get-ChildItem -Path $SampleDir -Filter *.vxa2 | Select-Object -First $discoverLimit
    foreach ($f in $vxa2Files) {
        if ($null -eq $baseVxa2Path) {
            $baseVxa2Path = $f.FullName
        }
        $tag = if ($isFullProfile) { "real-full" } else { "discovered" }
        Add-Entry -Entries $entries -SeenPaths $seenPaths -Name $f.Name -Path $f.FullName -Kind "VXA2" -Tag $tag
    }

    $xav2Files = Get-ChildItem -Path $SampleDir -Filter *.xav2 | Select-Object -First $discoverLimit
    foreach ($f in $xav2Files) {
        if ($null -eq $baseXav2Path) {
            $baseXav2Path = $f.FullName
        }
        $tag = if ($isFullProfile) { "real-full" } else { "discovered" }
        Add-Entry -Entries $entries -SeenPaths $seenPaths -Name $f.Name -Path $f.FullName -Kind "XAV2" -Tag $tag
    }

    $vrmFiles = Get-ChildItem -Path $SampleDir -Filter *.vrm | Select-Object -First 1
    if ($vrmFiles.Count -gt 0 -and $null -eq $baseVrmPath) {
        $baseVrmPath = $vrmFiles[0].FullName
    }
}

if ($null -eq $baseVrmPath) {
    $vrmFiles = Get-ChildItem -Path $SampleDir -Filter *.vrm | Select-Object -First 1
    if ($vrmFiles.Count -gt 0) {
        $baseVrmPath = $vrmFiles[0].FullName
    }
}

Add-GeneratedXav2FromVrm `
    -SampleDir $SampleDir `
    -OutputPath $OutputPath `
    -VrmToXav2Path $VrmToXav2Path `
    -Count $FixedXav2FromVrmCount `
    -Allowlist $FixedXav2FromVrmAllowlist `
    -FailOnAllowlistMiss ($UseFixedSet -or $isFullProfile) `
    -Entries $entries `
    -SeenPaths $seenPaths `
    -BaseXav2Path ([ref]$baseXav2Path) `
    -BaseVrmPath ([ref]$baseVrmPath)

if ($null -eq $baseXav2Path -and -not [string]::IsNullOrWhiteSpace($baseVrmPath)) {
    if (-not (Test-Path $VrmToXav2Path)) {
        throw "vrm_to_xav2 not found at $VrmToXav2Path and no fixed .xav2 sample was found"
    }
    $tmpDir = Join-Path (Split-Path -Parent $OutputPath) "..\\tmp_vx"
    if (-not (Test-Path $tmpDir)) {
        New-Item -ItemType Directory -Path $tmpDir | Out-Null
    }
    $generatedXav2Path = Join-Path $tmpDir "demo_mvp.xav2"
    & $VrmToXav2Path $baseVrmPath $generatedXav2Path | Out-Null
    if (-not (Test-Path $generatedXav2Path)) {
        throw "failed to generate fixed .xav2 sample via vrm_to_xav2"
    }
    $baseXav2Path = (Resolve-Path $generatedXav2Path).Path
    Add-Entry -Entries $entries -SeenPaths $seenPaths -Name ([System.IO.Path]::GetFileName($baseXav2Path)) -Path $baseXav2Path -Kind "XAV2" -Tag "fixed-valid"
}

if ($entries.Count -eq 0) {
    throw "no sample files selected under $SampleDir"
}
if ($null -eq $baseVxPath) {
    throw "could not locate base .vxavatar sample for synthetic cases"
}
if ($null -eq $baseVxa2Path) {
    throw "could not locate base .vxa2 sample for synthetic cases"
}
if ($null -eq $baseXav2Path) {
    throw "could not locate base .xav2 sample for synthetic cases"
}

$tmpDir = Join-Path (Split-Path -Parent $OutputPath) "..\\tmp_vx"
$synthetic = Build-SyntheticFiles -TmpDir $tmpDir -BaseVxPath $baseVxPath -BaseVxa2Path $baseVxa2Path -BaseXav2Path $baseXav2Path

Add-Entry -Entries $entries -SeenPaths $seenPaths -Name ([System.IO.Path]::GetFileName($synthetic["vx_truncated"])) -Path $synthetic["vx_truncated"] -Kind "VXAvatar" -Tag "synthetic-corrupt-vxavatar"
Add-Entry -Entries $entries -SeenPaths $seenPaths -Name ([System.IO.Path]::GetFileName($synthetic["vx_mismatch"])) -Path $synthetic["vx_mismatch"] -Kind "VXAvatar" -Tag "synthetic-corrupt-vxavatar"
Add-Entry -Entries $entries -SeenPaths $seenPaths -Name ([System.IO.Path]::GetFileName($synthetic["vxa2_truncated"])) -Path $synthetic["vxa2_truncated"] -Kind "VXA2" -Tag "synthetic-corrupt-vxa2"
Add-Entry -Entries $entries -SeenPaths $seenPaths -Name ([System.IO.Path]::GetFileName($synthetic["xav2_manifest_mismatch"])) -Path $synthetic["xav2_manifest_mismatch"] -Kind "XAV2" -Tag "synthetic-corrupt-xav2"
Add-Entry -Entries $entries -SeenPaths $seenPaths -Name ([System.IO.Path]::GetFileName($synthetic["xav2_truncated"])) -Path $synthetic["xav2_truncated"] -Kind "XAV2" -Tag "synthetic-corrupt-xav2"

"VXAvatar/VXA2/XAV2 probe report" | Set-Content -Path $OutputPath
"GateInputVersion: 3" | Add-Content -Path $OutputPath
"Generated: $(Get-Date -Format s)" | Add-Content -Path $OutputPath
"SampleDir: $(Resolve-Path $SampleDir)" | Add-Content -Path $OutputPath
"UseFixedSet: $UseFixedSet" | Add-Content -Path $OutputPath
"Profile: $Profile" | Add-Content -Path $OutputPath
"FileCount: $($entries.Count)" | Add-Content -Path $OutputPath
"" | Add-Content -Path $OutputPath

foreach ($entry in $entries) {
    "---- $($entry.Name)" | Add-Content -Path $OutputPath
    "  InputKind: $($entry.Kind)" | Add-Content -Path $OutputPath
    "  InputTag: $($entry.Tag)" | Add-Content -Path $OutputPath
    "  SourcePath: $($entry.Path)" | Add-Content -Path $OutputPath
    & $AvatarToolPath $entry.Path | Add-Content -Path $OutputPath
    "" | Add-Content -Path $OutputPath
}

Write-Host "Report written: $OutputPath"
