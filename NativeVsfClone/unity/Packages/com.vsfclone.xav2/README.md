# com.vsfclone.xav2

Unity package scaffold for XAV2.

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
