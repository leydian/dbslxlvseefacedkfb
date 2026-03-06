# XAV2 Format (Draft v1/v2/v3/v4/v5)

XAV2 is a vxa2-derived container focused on runtime-ready mesh/material transport.

## File Extension

- `.xav2`

## Binary Layout (v1/v2/v3/v4/v5)

1. `magic[4]`: ASCII `XAV2`
2. `version[2]`: little-endian unsigned integer (`1|2|3|4|5`)
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
- `skinningMatrixConvention`: string (`dx_row_major|gltf_column_major`)
- `skinSpaceBasis`: string (`mesh_local|unknown`)
- `skinningAutoCorrectedMeshes`: uint
- `skinningConflictResolvedMeshes`: uint
- `hasSkinning`: bool
- `hasBlendShapes`: bool
- `physicsSchemaVersion`: uint (`1`)
- `physicsSource`: string (`none|vrm|vrc|mixed`)
- `hasSpringBones`: bool
- `hasPhysBones`: bool

`skinningMatrixConvention` runtime default policy:
- If missing/unknown on XAV2, runtime uses `dx_row_major`.
- Auto-detect is disabled by default and can be enabled only for diagnostics with
  `VSFCLONE_XAV2_ENABLE_CONVENTION_AUTODETECT=1`.
- Legacy XAV2 without `skinSpaceBasis=mesh_local` may use compatibility auto-detect
  at load time to preserve old assets.

## TLV Section Header

1. `type[2]`
2. `flags[2]`
3. `size[4]`
4. `payload[size]`

If section payload crosses file boundary, loader returns `XAV2_SECTION_TRUNCATED`.

### Section Flags

- `0x0001`: payload is LZ4-compressed envelope (`v5+`)
  - payload layout:
    - `uncompressed_size[4]` (`uint32`, little-endian)
    - `compressed_lz4[...]`
- unknown flag bits are reserved; loader records `XAV2_SECTION_FLAGS_NONZERO` warning.

## Section Types (v1/v2/v3/v4/v5)

- `0x0001` Legacy mesh blob
- `0x0002` Texture blob
- `0x0003` Material override
- `0x0011` Mesh render payload
- `0x0012` Material shader params
- `0x0013` Skin payload
- `0x0014` BlendShape payload
- `0x0015` Material typed params (v2)
- `0x0016` Skeleton pose payload (v3)
- `0x0017` Skeleton rig payload (v4)
- `0x0018` SpringBone typed payload
- `0x0019` PhysBone typed payload
- `0x001A` Physics collider typed payload

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

### `0x0015` Material typed params (v2/v3)

- `name_len[2]`
- `name[name_len]`
- `shader_family_len[2]`
- `shader_family[shader_family_len]` (`standard|mtoon|liltoon|poiyomi|legacy`)
- `feature_flags[4]` (`uint32` bitmask)
- optional `typed_schema_version[2]` (`v3+`, current value `3`)
- `float_count[2]`
- repeated float entry:
  - `id_len[2]`
  - `id[id_len]`
  - `value[4]` (`float32`)
- `color_count[2]`
- repeated color entry:
  - `id_len[2]`
  - `id[id_len]`
  - `r[4]`, `g[4]`, `b[4]`, `a[4]` (`float32`)
- `texture_count[2]`
- repeated texture entry:
  - `slot_len[2]`
  - `slot[slot_len]`
  - `texture_ref_len[2]`
  - `texture_ref[texture_ref_len]`

For current implementation:

- `typed-v3` is preferred when present.
- `typed-v2` remains accepted for backward compatibility.
- typed fields are preferred over legacy `params_json` when both are present.

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

### `0x0016` Skeleton pose payload (v3)

- `mesh_name_len[2]`
- `mesh_name[mesh_name_len]`
- `matrix_f32_count[4]`
- `bone_matrices[matrix_f32_count * 4]` (`float32`)

Notes:

- `matrix_f32_count` should be a multiple of `16`.
- Each 16-float chunk is one `float4x4` bone matrix.
- In v3 skinning path, runtime combines section `0x0016` matrices with `0x0013` bindposes.

### `0x0017` Skeleton rig payload (v4)

- `mesh_name_len[2]`
- `mesh_name[mesh_name_len]`
- `bone_count[4]`
- repeated bone:
  - `bone_name_len[2]`
  - `bone_name[bone_name_len]`
  - `parent_index[4]` (`int32`, root=`-1`)
  - `local_matrix_f32_count[4]` (currently `16` required)
  - `local_matrix[local_matrix_f32_count * 4]` (`float32`)

Notes:

- For v4 skinning payloads (`0x0013` present), matching `0x0017` rig payload is expected.
- Missing rig for skinned mesh is reported as `XAV4_RIG_MISSING` (or strict-fail under strict validation).

### `0x0018` SpringBone typed payload

- `name_len[2]`, `name[name_len]`
- `root_bone_path_len[2]`, `root_bone_path[root_bone_path_len]`
- `bone_path_count[2]` + repeated sized string
- `stiffness[4]`, `drag[4]`, `radius[4]` (`float32`)
- `gravity_xyz[12]` (`float32 x,y,z`)
- `collider_ref_count[2]` + repeated sized string
- `enabled[1]` (`0|1`)

### `0x0019` PhysBone typed payload

- `name_len[2]`, `name[name_len]`
- `root_bone_path_len[2]`, `root_bone_path[root_bone_path_len]`
- `bone_path_count[2]` + repeated sized string
- `pull[4]`, `spring[4]`, `immobile[4]`, `radius[4]` (`float32`)
- `gravity_xyz[12]` (`float32 x,y,z`)
- `collider_ref_count[2]` + repeated sized string
- `enabled[1]` (`0|1`)

### `0x001A` Physics collider typed payload

- `name_len[2]`, `name[name_len]`
- `bone_path_len[2]`, `bone_path[bone_path_len]`
- `shape[1]` (`0=sphere,1=capsule,2=plane,255=unknown`)
- `radius[4]`, `height[4]` (`float32`)
- `local_position_xyz[12]` (`float32 x,y,z`)
- `local_direction_xyz[12]` (`float32 x,y,z`)

## Loader behavior in this repository

- Recognized extension: `.xav2`
- Unknown section policy:
  - `Warn` (default): unknown sections are skipped and warning `XAV2_UNKNOWN_SECTION` is emitted.
  - `Ignore`: unknown sections are skipped without warning.
  - `Fail`: load terminates with primary error `XAV2_UNKNOWN_SECTION_NOT_ALLOWED`.
- Loader diagnostics include both raw `warnings[]` and normalized `warning_codes[]`.
- Missing referenced payloads produce `XAV2_ASSET_MISSING` and `Compat: partial`.
- Warning-code contract (tooling/host):
  - `W_STAGE`: severity=`info`, category=`stage`
  - `W_LAYOUT|W_OFFSET|W_RECON_SUMMARY`: severity=`info`, category=`layout`
  - `W_*` (other): severity=`warn`, category=`payload`
  - `XAV2_*|XAV3_*|XAV4_*`: severity=`warn`, category=`render`
  - critical render codes:
    - `XAV2_SKINNING_STATIC_DISABLED`
    - `XAV2_MATERIAL_TYPED_TEXTURE_UNRESOLVED`
    - `XAV3_SKELETON_PAYLOAD_MISSING`
    - `XAV3_SKELETON_MESH_BIND_MISMATCH`
    - `XAV3_SKINNING_MATRIX_INVALID`
    - `XAV2_UNKNOWN_SECTION_NOT_ALLOWED`
- warning-code accumulation notes:
  - `W_STAGE` is treated as lifecycle telemetry and is not accumulated into `warning_codes[]`.
  - generic payload notes without stable code token are not accumulated into `warning_codes[]`.
  - `*_PARTIAL` compatibility notes are kept in `warnings[]` but excluded from `warning_codes[]`.

## 2026-03-06 Runtime Parity Fix Summary

This update resolves a set of XAV2 runtime parity regressions observed with VRM-derived avatars.

- Material alpha parity:
  - Root cause: runtime alpha heuristics could override XAV2 payload `alpha_mode` (example: `OPAQUE -> MASK`).
  - Fix: for XAV2, runtime now trusts payload `alpha_mode` when present and does not re-promote via heuristic path.
  - Validation target:
    - `MaterialParityMismatchCount = 0`
    - `MaterialParityLastMismatch = none`

- Typed material schema drift:
  - Root cause: `vrm_to_xav2` could emit typed payload body as legacy layout while manifest advertised `typed-v3`.
  - Fix:
    - exporter normalizes typed schema version to `>= v3` for XAV2 typed sections.
    - loader typed section parsing is made order-stable by deferring material-typed decode until after manifest options are known.
  - Validation target:
    - no `XAV2_MATERIAL_TYPED_SCHEMA_CONFLICT` warning for newly exported assets.

- Preview facing direction (front/back):
  - Root cause: preview yaw policy treated all XAV2 as fixed `0deg`, which can show back-facing view for VRM-derived exports.
  - Fix:
    - XAV2 loader stores manifest `sourceExt`.
    - runtime applies `180deg` preview yaw automatically for `sourceExt=.vrm`.
  - Validation target (`LastRenderPassSummary`):
    - `applied_preview_yaw_deg=180`
    - `preview_yaw_reason=xav2_vrm_source_180`

- Added diagnostics surfaced through host/native interop:
  - `material_parity_mismatch_count`
  - `texture_resolve_ambiguous_count`
  - `material_parity_last_mismatch`

Operational note:
- Runtime DLL replacement alone does not rewrite existing `.xav2` payloads.
- To pick up typed schema normalization fixes, re-export VRM to XAV2 with updated `vrm_to_xav2`.

## 2026-03-06 Follow-up Stabilization (Runtime)

Additional runtime hardening was applied after parity fixes to address legacy/edge XAV2 behavior:

- VRM-derived XAV2 static skinning policy:
  - For `sourceExt=.vrm`, static skinning is disabled in runtime (`bind-pose` rendering preferred).
  - Reason: mixed per-mesh static skinning outcomes can desynchronize face/hair/body placement.
  - Expected warning: `SKINNING_STATIC_DISABLED`.

- Legacy skinning convention fallback:
  - For legacy XAV2 without `skinSpaceBasis`, runtime convention hint prefers VRM-origin defaults
    (`dx_row_major` when `sourceExt=.vrm`) instead of forcing glTF path.

- Outlier mesh handling consistency:
  - Meshes excluded by preview bounds filter are now also excluded in draw phase.
  - Added detached cluster filtering using robust center statistics (median-based) to suppress
    floating accessory clusters that corrupt framing.
  - Related warning codes:
    - `XAV2_BOUNDS_OUTLIER_DRAW_SKIPPED`
    - `XAV2_DETACHED_MESH_OUTLIER_SKIPPED`
    - `XAV2_DETACHED_CLUSTER_SKIPPED`

- AutoFit bust framing stability:
  - `focus_y` in `AutoFitBust` now blends bounds-derived and robust-center-derived anchors for XAV2,
    especially when outlier meshes are excluded.
  - Goal: reduce "avatar sunk into ground" framing after outlier filtering.
