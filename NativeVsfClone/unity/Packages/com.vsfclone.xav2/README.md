# com.vsfclone.xav2

Unity package for XAV2 export/runtime parsing.

## Scope (v0.1.0)

- Unity `2022.3 LTS`
- Built-in Render Pipeline
- Export-first workflow (`Scene AvatarRoot -> .xav2`)

## Shader policy (strict)

- `lilToon`
- `Poiyomi`
- `potatoon`
- `realtoon`

If a material in the export target set references a shader outside this list, exporter fails by default.

## Editor entry

- `Tools/VsfClone/XAV2/Export Selected AvatarRoot`

## Runtime loader API

- `Xav2RuntimeLoader.Load(path)`
  - throw-on-failure path for simple call sites
- `Xav2RuntimeLoader.TryLoad(path, out payload, out diagnostics)`
  - non-throwing path with stage/error diagnostics
- `Xav2LoadDiagnostics`
  - `ErrorCode`, `ErrorMessage`, `ParserStage`, `IsPartial`, `Warnings`
