# Workspace Interim Full Change Summary (2026-03-06)

## Scope

- This report summarizes all in-flight repository changes observed before commit/push.
- Window covered: current working tree delta on `main` as of 2026-03-06.
- Includes code, tooling, docs restructuring, Unity editor updates, and generated report deltas.

## Implemented Changes

### 1. Runtime load/race hardening (HostCore)

- `HostController` now guards runtime mutations and render tick with `_runtimeSync` to avoid load/render races.
- Render tick is suppressed while `LoadAvatar` is active, with explicit diagnostics for suppressed/resumed states.
- Busy operations now execute under the runtime lock for state consistency.
- `LoadAvatarAsync` now prevents overlapping worker tasks, tracks timeout/cancel as pending-safe completion, and discards late completions safely.
- Cancel flow was changed to "cancel pending" semantics while worker finalization completes.
- Avatar swap semantics were hardened:
  - previous avatar handle/info are preserved on failed load,
  - previous resources are destroyed only after successful replacement,
  - failure during swap logs `AvatarSwapAborted` with preserved active avatar.

### 2. Native material diagnostics expansion

- `NcAvatarInfo` contract extended with material-mode counters:
  - `opaque_material_count`, `mask_material_count`, `blend_material_count`.
- Added `last_render_pass_summary` to native interop payload.
- WPF/WinUI diagnostics panels now show material mode counts and render pass summary.
- Native package diagnostics (`MaterialDiagnosticsEntry`) now includes `alpha_source`.
- Native summary text now emits `alphaSource` in material diagnostics output.

### 3. VRM alpha-mode inference refinement

- `vrm_loader.cpp` adds a broader alpha-resolution strategy with fallback hints from:
  - `material` core properties,
  - `material.extensions` and MToon extension fields,
  - `material.extras`,
  - VRM0 `extensions.VRM.materialProperties` (index + name match).
- Introduced helper logic for bool-like coercion, mode normalization, queue-based hints, and cutoff handling.
- Diagnostics now record both final alpha mode and hint source (`alpha_source`).

### 4. Tooling and gate updates

- `avatar_tool.cpp` now prints opaque/mask/blend material counts and includes `alphaSource` in last material diagnostics.
- `vrm_quality_gate.ps1` adds GateG reporting for blend-material coverage (with informational `no-blend-sample` mode).
- `publish_hosts.ps1` adds offline NuGet source controls for publish/diagnostics:
  - `-NuGetSourceMode default|offline-only`
  - `-OfflineSourcePath`
  - shared NuGet publish args injection and `NoRestore` hardening (`/p:Restore=false`).
- `docs_quality_gate.ps1` upgraded for the new doc architecture:
  - validates weekly structure (`INDEX.md`, `SUMMARY.md`),
  - validates canonical naming pattern,
  - validates legacy stub mapping and target integrity,
  - validates required docs hubs (`DOMAIN_INDEX.md`, `legacy-map.md`).

### 5. Unity editor export defaults and tests

- `MiqExportMenu` now centralizes default export options through `CreateDefaultExportOptions(...)`.
- Default export path now enables compression (`LZ4/Balanced`) and logs compression mode.
- Package README updated to reflect default compression behavior.
- Added internal test exposure and editor tests validating strict/relaxed default options.

### 6. Documentation system restructuring (major)

- Report system migrated from flat `docs/reports/*.md` to weekly canonical layout:
  - `docs/reports/weekly/2026-W10/*.md`.
- Current migration state:
  - `92` legacy report files converted to redirect stubs,
  - `92` canonical weekly report files created,
  - `92` archive snapshots created under `docs/archive/reports-legacy/`.
- New navigation assets introduced:
  - `docs/reports/weekly/INDEX.md`,
  - `docs/reports/DOMAIN_INDEX.md`,
  - `docs/reports/legacy-map.md`.
- Docs policy updated in `docs/CONTRIBUTING_DOCS.md`.
- Root docs index replaced with hub-based navigation in `docs/INDEX.md`.

### 7. Generated report updates

- Build report snapshots were refreshed:
  - `build/reports/vrm_gate_fixed5.txt`,
  - `build/reports/vrm_probe_fixed5.txt`.

### 8. Untracked artifacts observed during this window

- Local temp/vendor artifacts are present but not part of core source changes:
  - `NativeAnimiq/third_party/_spout_tmp/**`,
  - `Spout-SDK-binaries_2-007-017_1.zip`,
  - `build/reports/*.json` and `build/reports/*.txt` (root-level build dir),
  - one local `.miq` sample file at repo root.

## Verification Summary

- Working tree aggregate before commit:
  - `112 files changed, 1126 insertions(+), 11472 deletions(-)`.
- Scope-level diff stats:
  - `build`: 2 files, `+43/-7`
  - `docs`: 94 files, `+415/-11381`
  - `host`: 6 files, `+168/-27`
  - `include`: 2 files, `+5/-0`
  - `src`: 2 files, `+250/-2`
  - `tools`: 4 files, `+228/-50`
  - `unity`: 2 files, `+17/-5`
- Documentation gate execution:
  - `powershell -ExecutionPolicy Bypass -File .\NativeAnimiq\tools\docs_quality_gate.ps1`
  - Result: pass (no broken links, no missing weekly indexes/summaries, no mapping breaks, no UTF-8 failures).

## Known Risks or Limitations

- Large documentation migration means downstream consumers with hard-coded legacy paths should rely on `legacy-map.md` and stubs during transition.
- Temporary Spout SDK extraction and root-level generated artifacts remain untracked and should be intentionally curated if promotion is required.

## Next Steps

1. Commit staged source/docs/tooling changes with this report.
2. Push `main` to origin.
3. Optionally decide whether to formalize or ignore currently untracked vendor/temp artifacts.
