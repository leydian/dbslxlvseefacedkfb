# MIQ extreme detached-cluster draw guard + AutoFit bust stabilization (2026-03-06)

## Scope

- This report covers runtime-side MIQ rendering/framing stabilization for:
  - floating detached small clusters causing visible part separation
  - bust AutoFit anchor instability causing "sunk into ground" framing feel
- Out of scope:
  - exporter payload/schema regeneration
  - host-side UI/flow changes
  - static skinning policy changes for `sourceExt=.vrm` (kept as-is)

## Implemented Changes

- Primary code path:
  - `src/nativecore/native_core.cpp`

- AutoFit bounds cluster filter tightening:
  - `bounds_cluster_distance_threshold`
    - before: `max(3.8f, median_extent * 4.5f)`
    - after: `max(2.2f, median_extent * 2.8f)`
  - new cap for cluster-filter candidates:
    - `cluster_bounds_extent_cap = max(0.75f, median_extent * 1.4f)`
    - cluster-distance filter now applies only when `sample.extent <= cluster_bounds_extent_cap`

- Bust framing stabilization:
  - MIQ `AutoFitBust` blend updated:
    - outlier-filtered case: `0.70 -> 0.78`
    - non-filtered case: `0.45 -> 0.52`
  - `focus_y` clamp introduced:
    - min: `avatar_bmin.y + safe_extent_y * 0.42`
    - max: `avatar_bmin.y + safe_extent_y * 0.82`

- Draw-phase safety override in default `autofit_only` mode:
  - retained policy split (`autofit_only` default, `skip_draw` legacy)
  - added unconditional extreme-detached skip for MIQ small clusters:
    - `robust_dist > max(2.4f, median_center_dist * 5.5f)`
    - `emax <= max(0.35f, median_extent * 1.1f)`
  - warning code added:
    - `MIQ_EXTREME_DETACHED_CLUSTER_SKIPPED`

- Diagnostics enrichment:
  - preview debug string now includes:
    - `bounds_cluster_threshold`
    - `bounds_cluster_extent_cap`
    - `extreme_detached_skipped`

- Contract/docs update:
  - `docs/formats/miq.md`
    - documented that even under `autofit_only`, extreme detached small clusters can be skipped for render safety
    - added `MIQ_EXTREME_DETACHED_CLUSTER_SKIPPED` to related warning codes

## Verification Summary

- Build:
  - `cmake --build NativeAnimiq/build --config Release --target nativecore` -> PASS

- Sample load smoke:
  - `NativeAnimiq/build/Release/avatar_tool.exe "D:\dbslxlvseefacedkfb\개인작10-2.miq" --dump-warnings-limit=50` -> PASS
  - `Compat: full`, no loader failure introduced

- Runtime deployment sync:
  - runtime process lock detected on `dist/wpf/nativecore.dll` (`WpfHost.exe`)
  - stopped process, copied latest DLL, rechecked SHA256 parity:
    - `build/Release/nativecore.dll` == `dist/wpf/nativecore.dll`

## Known Risks or Limitations

- Thresholds are heuristic and tuned for robustness over strict determinism across all avatar topologies.
- Extreme-detached skip can still hide tiny legitimate meshes if they satisfy both distance/size conditions.
- Final visual validation remains runtime-scene dependent; `avatar_tool` validates load contract, not final camera composition.

## Next Steps

1. Run `tools/miq_render_regression_gate.ps1` against broader MIQ sample set and compare warning deltas.
2. Capture before/after preview debug strings for problematic samples and tune thresholds only if false skips are observed.
3. If needed, expose extreme-detached safety override as an explicit env toggle for operator control.
