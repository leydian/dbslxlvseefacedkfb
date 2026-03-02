# VXA2 Format (Draft v1)

VXA2 is the next-step avatar container format for gradual migration from `.vxavatar`.

## File Extension

- `.vxa2`

## Binary Layout (v1)

1. `magic[4]`: ASCII `VXA2`
2. `version[2]`: little-endian unsigned integer (`1`)
3. `manifest_size[4]`: little-endian unsigned integer
4. `manifest_json[manifest_size]`: UTF-8 JSON
5. `asset_sections[...]`: zero or more TLV entries

### Asset Section TLV (v1)

Each section entry is appended immediately after the previous entry:

1. `type[2]`: little-endian unsigned integer
2. `flags[2]`: little-endian unsigned integer (currently `0` expected)
3. `size[4]`: little-endian unsigned integer
4. `payload[size]`

Runtime validates section boundaries strictly. If any section header/payload crosses the file boundary, loading exits with `VXA2_SECTION_TRUNCATED`.

### Section Types (v1)

- `0x0001` Mesh blob section:
  - `name_len[2]`
  - `name[name_len]` (UTF-8)
  - `blob_size[4]`
  - `blob[blob_size]`
- `0x0002` Texture blob section:
  - `name_len[2]`
  - `name[name_len]` (UTF-8)
  - `blob_size[4]`
  - `blob[blob_size]`
- `0x0003` Material override section:
  - `name_len[2]`
  - `name[name_len]`
  - `shader_len[2]`
  - `shader[shader_len]`
  - `base_color_texture_len[2]`
  - `base_color_texture_name[base_color_texture_len]`

Unknown section types are skipped for forward compatibility and surfaced as warnings.

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
- TLV asset sections are decoded for mesh/texture/material override payloads (`type=0x0001/0x0002/0x0003`).
- Unknown section types are skipped and reported.
- Missing manifest-ref payloads are reported as `VXA2_ASSET_MISSING` with `Compat: partial`.
