# XAV2 Format (Draft v1)

XAV2 is a vxa2-derived container focused on runtime-ready mesh/material transport.

## File Extension

- `.xav2`

## Binary Layout (v1)

1. `magic[4]`: ASCII `XAV2`
2. `version[2]`: little-endian unsigned integer (`1`)
3. `manifest_size[4]`: little-endian unsigned integer
4. `manifest_json[manifest_size]`: UTF-8 JSON
5. `asset_sections[...]`: zero or more TLV entries

## Manifest Required Keys

- `avatarId`: string
- `meshRefs`: string array
- `materialRefs`: string array
- `textureRefs`: string array

## Manifest Suggested Keys

- `displayName`: string
- `sourceExt`: string (example: `.vrm`)
- `strictShaderSet`: string array
- `schemaVersion`: uint (`1`)
- `exporterVersion`: string
- `hasSkinning`: bool
- `hasBlendShapes`: bool

## TLV Section Header

1. `type[2]`
2. `flags[2]` (currently `0` expected)
3. `size[4]`
4. `payload[size]`

If section payload crosses file boundary, loader returns `XAV2_SECTION_TRUNCATED`.

## Section Types (v1)

- `0x0001` Legacy mesh blob
- `0x0002` Texture blob
- `0x0003` Material override
- `0x0011` Mesh render payload
- `0x0012` Material shader params
- `0x0013` Skin payload
- `0x0014` BlendShape payload

### `0x0011` Mesh render payload

- `name_len[2]`
- `name[name_len]`
- `vertex_stride[4]`
- `material_index[4]` (signed int32)
- `vertex_blob_size[4]`
- `vertex_blob[vertex_blob_size]`
- `index_count[4]`
- `indices[index_count * 4]` (`uint32` each)

### `0x0003` Material override

- `name_len[2]`
- `name[name_len]`
- `shader_len[2]`
- `shader[shader_len]`
- `shader_variant_len[2]`
- `shader_variant[shader_variant_len]`
- `base_color_texture_len[2]`
- `base_color_texture_name[base_color_texture_len]`
- `alpha_mode_len[2]`
- `alpha_mode[alpha_mode_len]`
- `alpha_cutoff[4]` (`float32`)
- `double_sided[1]` (`0|1`)

### `0x0012` Material shader params

- `name_len[2]`
- `name[name_len]`
- `params_json_len[2]`
- `params_json[params_json_len]`

`params_json` is a shader-specific payload (for example lilToon/Poiyomi parameter blocks).

### `0x0013` Skin payload

- `mesh_name_len[2]`
- `mesh_name[mesh_name_len]`
- `bone_count[4]`
- `bone_indices[bone_count * 4]` (`int32`)
- `bindpose_f32_count[4]`
- `bindposes[bindpose_f32_count * 4]` (`float32`)
- `skin_weight_blob_size[4]`
- `skin_weight_blob[skin_weight_blob_size]`

### `0x0014` BlendShape payload

- `mesh_name_len[2]`
- `mesh_name[mesh_name_len]`
- `frame_count[4]`
- repeated frame:
  - `frame_name_len[2]`
  - `frame_name[frame_name_len]`
  - `weight[4]` (`float32`)
  - `delta_vertices_size[4]`
  - `delta_vertices[delta_vertices_size]`
  - `delta_normals_size[4]`
  - `delta_normals[delta_normals_size]`
  - `delta_tangents_size[4]`
  - `delta_tangents[delta_tangents_size]`

## Loader behavior in this repository

- Recognized extension: `.xav2`
- Unknown section policy:
  - `Warn` (default): unknown sections are skipped and warning `XAV2_UNKNOWN_SECTION` is emitted.
  - `Ignore`: unknown sections are skipped without warning.
  - `Fail`: load terminates with primary error `XAV2_UNKNOWN_SECTION_NOT_ALLOWED`.
- Loader diagnostics include both raw `warnings[]` and normalized `warning_codes[]`.
- Missing referenced payloads produce `XAV2_ASSET_MISSING` and `Compat: partial`.
