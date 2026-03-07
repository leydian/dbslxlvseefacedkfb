# 2026-03-08 VRM Loader Mesh Loop Compile Recovery and WPF Redeploy

## Summary

Addressed a compile-blocking regression in `vrm_loader.cpp` mesh processing by restoring missing variable declarations and scope initialization.
After recovery, native build and VRM load smoke both passed, and WPF distribution was republished with updated `nativecore.dll`.

## Symptoms

- Release build failed with unresolved identifiers in `vrm_loader.cpp`:
  - `has_vrm_normals`
  - `vrm_normals`
  - `emitted`
- Failure blocked `animiq_core`, `nativecore`, and host redistributable refresh.

## Root Cause

During mesh-loop restructuring, the primitive-local normal extraction block was removed or displaced while downstream logic still referenced:

- `has_vrm_normals` for normal bake/interleave path
- `vrm_normals` for normal rotation/interleaving

Additionally, skinned payload status tracking still depended on `emitted` but no longer declared it in branch scope.

## Implemented Fix

Target file:

- `AnimiqCore/src/avatar/vrm_loader.cpp`

Changes:

1. Restored primitive-local normal extraction setup immediately after position extraction:
   - `std::vector<std::array<float, 3U>> vrm_normals`
   - `const auto* normal_v = FindKey(*attrs_v, "NORMAL")`
   - `const bool has_vrm_normals = ... ExtractNormals(...) && size == vtx_count`
2. Restored skinned-branch status flag:
   - `bool emitted = false;` at `if (mesh_has_skin[mesh_i])` entry.
3. Kept existing runtime behavior order unchanged:
   - normal extraction
   - node transform position baking
   - skinned IBM correction

No extra heuristic policy change was introduced in this pass; scope was compile recovery and safe redeploy.

## Verification

### Build

- Command:
  - `cmake --build .\build --config Release --target avatar_tool`
- Result: PASS

### Loader smoke

- Command:
  - `.\build\Release\avatar_tool.exe .\sample\NewOnYou.vrm --dump-warnings-limit=60`
- Result: PASS
- Key line:
  - `W_SKIN: VRM_SKIN_PAYLOAD_STATUS: skinnedPrimitives=30, emitted=30, failed=0`

### WPF redeploy

- Command:
  - `powershell -ExecutionPolicy Bypass -File .\tools\publish_hosts.ps1`
- Result: PASS
- Outputs:
  - `dist/wpf/WpfHost.exe` refreshed
  - `dist/wpf/nativecore.dll` refreshed from rebuilt `build/Release/nativecore.dll`
  - `wpf_launch_smoke_latest.txt`: PASS

## Notes

- Existing unrelated local workspace modifications were intentionally left untouched.
- Untracked browser-temp workspace content under `host/Branding/tmp_edge/` remains out of this change scope.
