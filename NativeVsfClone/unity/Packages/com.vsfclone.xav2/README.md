# com.vsfclone.xav2

Unity package for XAV2 export/runtime parsing.

## Scope (v0.1.0)

- Unity `2021.3.18f1+` (LTS line)
- Built-in Render Pipeline
- Export-first workflow (`Scene AvatarRoot -> .xav2`)

## Shader policy (strict)

- `lilToon`
- `Poiyomi`
- `potatoon`
- `realtoon`

If a material in the export target set references a shader outside this list, exporter fails by default.

## Editor entry

- Export:
  - `Tools/VsfClone/XAV2/Export Selected AvatarRoot`
- Import:
  - `Tools/VsfClone/XAV2/Import XAV2...`
  - Output root default: `Assets/ImportedXav2/<avatarId>/`
  - Collision policy default: unique suffix (`_1`, `_2`, ...)
  - Partial import default: continue with warnings
  - v4 skinning import prefers skeleton rig payload (`0x0017`) for bone hierarchy reconstruction
  - Rig policy defaults:
    - `FailOnRigDataMissing = true`
    - `RigRecoveryPolicy = Strict`

## Runtime loader API

- `Xav2RuntimeLoader.Load(path)`
  - throw-on-failure path for simple call sites
- `Xav2RuntimeLoader.TryLoad(path, out payload, out diagnostics)`
  - non-throwing path with stage/error diagnostics
- `Xav2RuntimeLoader.TryLoad(path, out payload, out diagnostics, options)`
  - option-based path (`Xav2LoadOptions.StrictValidation`, `Xav2LoadOptions.UnknownSectionPolicy`)
- `Xav2LoadDiagnostics`
  - `ErrorCode`, `ErrorMessage`, `ParserStage`, `IsPartial`, `Warnings`, `WarningCodes`

## Export compression (v5)

- `Xav2ExportOptions.EnableCompression` enables section-level payload compression.
- Current codec: `LZ4` (`Xav2CompressionCodec.Lz4`).
- Large sections are compressed opportunistically (`mesh`, `texture`, `skin`, `blendshape`) when size improves.
- Compression-enabled exports are written as format `v5`; loader still supports `v1..v4`.

Unknown section policy:

- `Warn` (default): append warning (`XAV2_UNKNOWN_SECTION`) and continue
- `Ignore`: skip unknown section silently
- `Fail`: stop with `UnknownSectionNotAllowed`

## Tests

- Runtime tests:
  - `Tests/Runtime/Xav2RuntimeLoaderTests.cs`
- Editor import tests:
  - `Tests/Editor/Xav2ImporterTests.cs`
- Recommended execution:
  - Unity Editor Test Runner (EditMode)

## CI validation (2021.3.18f1)

- Support is gated by:
  - EditMode tests (`-runTests -testPlatform EditMode`)
  - export smoke (`Xav2CiSmoke` export path)
  - load smoke (`Xav2RuntimeLoader.TryLoad`, `runtime-ready`)
- Local reproduction:
  - `powershell -ExecutionPolicy Bypass -File .\tools\unity_xav2_validate.ps1 -UnityEditorPath "<Unity.exe>" -UnityProjectPath "<UnityProjectPath>" -ExpectedUnityVersion "2021.3.18f1"`
