# MIQ Header Compatibility Hotfix and WPF Redeploy (2026-03-06)

## Summary

Resolved avatar load failures (`Unsupported` / `MIQ_SCHEMA_INVALID`) caused by MIQ header contract drift across runtime components, and completed WPF runtime redeploy with validated dist artifacts.

Primary outcomes:

- native MIQ loader now accepts both `MIQ2` (current native contract) and legacy Unity-style `MIQ` header layouts,
- byte-signature probing for MIQ no longer checks stale `XAV2`,
- user-reported sample (`개인작10-2.miq`) moved from parse-fail to runtime-ready load state,
- WPF host publish is completed to `dist/wpf` with launch-smoke pass.

## Root Cause

The runtime pipeline had mixed header expectations after migration:

- Native loader strict check:
  - required first 4 bytes `MIQ2`
  - file with 3-byte `MIQ` header was rejected at parse stage (`MIQ_SCHEMA_INVALID: magic header mismatch`)
- Unity MIQ exporter/runtime path:
  - writes/checks `MIQ` header form
- Additional migration gap:
  - `MiqLoader::CanLoadBytes(...)` still checked legacy `XAV2` bytes.

This mismatch produced host-level `Unsupported` outcomes even when extension was `.miq` and payload sections were otherwise valid.

## Implementation Changes

- Native MIQ loader compatibility update:
  - `src/avatar/Miq_loader.cpp`
  - `CanLoadBytes(...)` updated from stale `XAV2` signature check to MIQ family check (`M`, `I`, `Q`)
  - header parsing now supports:
    - `MIQ2` layout: 4-byte header + `u16 version` + `u32 manifest_size`
    - legacy `MIQ` layout: 3-byte header + `u16 version` + `u32 manifest_size`
  - `version` and `manifest` offsets are now derived from detected header size.
- WPF publish unblock fix:
  - `host/WpfHost/MainWindow.xaml.cs`
  - resolved C# scope collision (`CS0136`) by renaming inner local variable `reason` -> `runtimeReason` in `BuildCommonCauseTriageLine(...)`.

## Verification

### 1) Repro and root-cause evidence

- Sample header bytes (`개인작10-2.miq`) before fix:
  - `4D 49 51 05 00 ...` (`MIQ` + version)
- Native tool repro before fix:
  - result: `Compat: failed`, `ParserStage: parse`, `PrimaryError: MIQ_SCHEMA_INVALID`
  - warning: `E_PARSE: MIQ_SCHEMA_INVALID: magic header mismatch.`

### 2) Loader hotfix validation

- Built updated native tool:
  - `cmake --build AnimiqCore/build_plan_impl --config Release --target avatar_tool`
- Re-ran same sample on updated tool:
  - result: `Compat: full`, `ParserStage: runtime-ready`, `PrimaryError: NONE`
  - payload counts restored (`MeshPayloads: 35`, `MaterialPayloads: 14`, etc.).

### 3) Host publish/redeploy validation

- Publish command:
  - `powershell -ExecutionPolicy Bypass -File .\tools\publish_hosts.ps1`
- Publish status:
  - WPF publish completed (`dist/wpf`)
  - WPF launch smoke: `PASS`
  - output exe: `dist/wpf/WpfHost.exe`
- Artifact integrity:
  - `nativecore.dll` source/destination SHA256 match confirmed in `build/reports/host_publish_latest.txt`.

## Operator Impact

- Existing `.miq` files produced with legacy `MIQ` header no longer fail immediately in native parse stage.
- Host runtime should no longer collapse this class of issue into generic load failure for valid legacy-header MIQ payloads.
- Deployment should use latest `dist/wpf/WpfHost.exe` and paired `nativecore.dll` to ensure fix presence.
