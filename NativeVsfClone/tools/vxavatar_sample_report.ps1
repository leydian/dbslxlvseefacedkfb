param(
    [string]$SampleDir = "..\\sample",
    [string]$AvatarToolPath = ".\\build\\Release\\avatar_tool.exe",
    [string]$OutputPath = ".\\build\\reports\\vxavatar_probe_latest.txt",
    [int]$MaxFiles = 20,
    [switch]$UseFixedSet,
    [string[]]$FixedVxSamples = @(
        "demo_mvp.vxavatar"
    ),
    [string[]]$FixedVxa2Samples = @(
        "demo_mvp.vxa2"
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
        [string]$BaseVxa2Path
    )

    if (-not (Test-Path $TmpDir)) {
        New-Item -ItemType Directory -Path $TmpDir | Out-Null
    }

    $out = @{}

    $vxTruncatedPath = Join-Path $TmpDir "demo_mvp_truncated.vxavatar"
    $vxMismatchPath = Join-Path $TmpDir "demo_mvp_cd_mismatch.vxavatar"
    $vxa2TruncatedPath = Join-Path $TmpDir "demo_tlv_truncated.vxa2"

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

    $out["vx_truncated"] = $vxTruncatedPath
    $out["vx_mismatch"] = $vxMismatchPath
    $out["vxa2_truncated"] = $vxa2TruncatedPath
    return $out
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
$baseVxPath = $null
$baseVxa2Path = $null

if ($UseFixedSet) {
    foreach ($name in $FixedVxSamples) {
        $p = Join-Path $SampleDir $name
        if (Test-Path $p) {
            $entries.Add([PSCustomObject]@{
                Name = [System.IO.Path]::GetFileName($p)
                Path = (Resolve-Path $p).Path
                Kind = "VXAvatar"
                Tag = "fixed-valid"
            })
            if ($null -eq $baseVxPath) { $baseVxPath = (Resolve-Path $p).Path }
        }
    }

    foreach ($name in $FixedVxa2Samples) {
        $p = Join-Path $SampleDir $name
        if (Test-Path $p) {
            $entries.Add([PSCustomObject]@{
                Name = [System.IO.Path]::GetFileName($p)
                Path = (Resolve-Path $p).Path
                Kind = "VXA2"
                Tag = "fixed-valid"
            })
            if ($null -eq $baseVxa2Path) { $baseVxa2Path = (Resolve-Path $p).Path }
        }
    }
} else {
    $vxFiles = Get-ChildItem -Path $SampleDir -Filter *.vxavatar | Select-Object -First $MaxFiles
    foreach ($f in $vxFiles) {
        $entries.Add([PSCustomObject]@{
            Name = $f.Name
            Path = $f.FullName
            Kind = "VXAvatar"
            Tag = "discovered"
        })
    }

    $vxa2Files = Get-ChildItem -Path $SampleDir -Filter *.vxa2 | Select-Object -First $MaxFiles
    foreach ($f in $vxa2Files) {
        $entries.Add([PSCustomObject]@{
            Name = $f.Name
            Path = $f.FullName
            Kind = "VXA2"
            Tag = "discovered"
        })
    }

    if ($vxFiles.Count -gt 0) {
        $baseVxPath = $vxFiles[0].FullName
    }
    if ($vxa2Files.Count -gt 0) {
        $baseVxa2Path = $vxa2Files[0].FullName
    }
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

$tmpDir = Join-Path (Split-Path -Parent $OutputPath) "..\\tmp_vx"
$synthetic = Build-SyntheticFiles -TmpDir $tmpDir -BaseVxPath $baseVxPath -BaseVxa2Path $baseVxa2Path

$entries.Add([PSCustomObject]@{
    Name = [System.IO.Path]::GetFileName($synthetic["vx_truncated"])
    Path = $synthetic["vx_truncated"]
    Kind = "VXAvatar"
    Tag = "synthetic-corrupt-vxavatar"
})
$entries.Add([PSCustomObject]@{
    Name = [System.IO.Path]::GetFileName($synthetic["vx_mismatch"])
    Path = $synthetic["vx_mismatch"]
    Kind = "VXAvatar"
    Tag = "synthetic-corrupt-vxavatar"
})
$entries.Add([PSCustomObject]@{
    Name = [System.IO.Path]::GetFileName($synthetic["vxa2_truncated"])
    Path = $synthetic["vxa2_truncated"]
    Kind = "VXA2"
    Tag = "synthetic-corrupt-vxa2"
})

"VXAvatar/VXA2 probe report" | Set-Content -Path $OutputPath
"GateInputVersion: 1" | Add-Content -Path $OutputPath
"Generated: $(Get-Date -Format s)" | Add-Content -Path $OutputPath
"SampleDir: $(Resolve-Path $SampleDir)" | Add-Content -Path $OutputPath
"UseFixedSet: $UseFixedSet" | Add-Content -Path $OutputPath
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
