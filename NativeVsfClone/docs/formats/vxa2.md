# VXA2 Format (Draft v1)

VXA2 is the next-step avatar container format for gradual migration from `.vxavatar`.

## File Extension

- `.vxa2`

## Binary Layout (v1)

1. `magic[4]`: ASCII `VXA2`
2. `version[2]`: little-endian unsigned integer (`1`)
3. `manifest_size[4]`: little-endian unsigned integer
4. `manifest_json[manifest_size]`: UTF-8 JSON
5. `asset_sections[...]`: optional in v1 draft (runtime support pending)

## Manifest Required Keys

- `avatarId`: string
- `meshRefs`: string array
- `materialRefs`: string array
- `textureRefs`: string array

## Manifest Optional Keys

- `displayName`: string
- `shaderProfile`: string
- `metadata`: object

## Validation Rules

- `magic` must be `VXA2`.
- `version` must be supported by runtime (`1` for current draft).
- `manifest_size` must stay inside file boundary.
- Required keys must exist and be valid types.
- References must be non-empty strings.

## Compatibility Rules

- Backward compatibility:
  - Readers must reject unknown mandatory binary structure changes by version.
- Forward compatibility:
  - Unknown manifest keys must be ignored.
  - Additional asset sections may be safely skipped if section descriptor is unknown.

## Current Runtime Status (This Repository)

- Loader recognizes `.vxa2` and validates header + manifest section.
- Required manifest keys are validated.
- Payload name mapping is created for mesh/material/texture references.
- Binary asset section decode is not implemented yet and is reported via diagnostics.
