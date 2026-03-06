# Weekly Summary 2026-W10

## Scope
- This summary groups weekly changes by domain.

## Highlights
- `xav2`: 26 reports
- `host`: 13 reports
- `ui`: 9 reports
- `vsfavatar`: 8 reports
- `r01`: 4 reports
- `vxavatar`: 4 reports
- `wpf`: 15 reports
- `release`: 3 reports
- `vrm`: 14 reports
- `winui`: 3 reports
- `workspace`: 3 reports
- `avatar`: 2 reports
- `platform`: 2 reports
- `session`: 2 reports
- `tracking`: 2 reports
- `webcam`: 3 reports
- `consumer`: 1 reports
- `ifacialmocap`: 1 reports
- `nativecore`: 2 reports
- `arkit52`: 2 reports
- `runtime`: 1 reports
- `spout2`: 1 reports
- `vxa2`: 1 reports

## Documentation Optimization Update (2026-03-06)
- Quality gate status: `PASS` (`docs_quality_gate.ps1`)
- Verified checks:
- broken `docs/INDEX.md` links `0`
- missing weekly `INDEX.md`/`SUMMARY.md` `0`
- invalid canonical names `0`
- legacy stubs missing map `0`
- legacy stubs with invalid target `0`
- legacy map rows with missing target `0`
- UTF-8 invalid files `0`
- Structural cleanup completed:
- `docs/reports/legacy-map.md` rebuilt to current canonical/legacy/archive mapping (`114` entries)
- legacy report redirects normalized (required `New location` + `Archived original snapshot` link contract)
- archive snapshots backfilled under `docs/archive/reports-legacy/` for missing legacy snapshots (`22` files)
- Weekly canonical doc normalization:
- `2026-03-06_wpf_render_only_mode_toggle.md` re-encoded to valid UTF-8 for gate compatibility

## Next Steps
- Pick three key reports and review promotion to long-term references.
- Newly added: review `2026-03-06_vrm_runtime_metrics_v2_api.md` for runtime quality metric adoption and gate integration.
- Newly added: review `2026-03-06_webcam_device_enumeration_and_tracking_refresh.md` for Windows camera enumeration contract, index-key sidecar compatibility, and WPF/WinUI refresh/fallback behavior.
- Newly added: review `2026-03-06_vrm_springbone_mtoon_runtime_refinement.md` for SpringBone solver and MToon advanced runtime uplift validation notes.
- Newly added: review `2026-03-06_onboarding_kpi_summary_automation.md` for diagnostics KPI rollup automation and operational gating readiness.
- Newly added: review `2026-03-06_wpf_avatar_preview_worker_thumbnails.md` for WPF pre-load avatar 3D thumbnail worker architecture, persistence schema v7, and queue/timeout behavior.
- Newly added: review `2026-03-06_wpf_arm_chain_coupling_shoulder_lowerarm_hand.md` for upper-arm driven shoulder/lower-arm/hand coupling model, per-bone pitch clamp policy, and native static-skinning chain application coverage.
- Newly added: review `2026-03-06_wpf_render_only_mode_toggle.md` for WPF render-only mode toggle, F11 UX, and layout/resize sync behavior.
- Newly added: review `2026-03-06_wpf_arm_pose_slider_wiring.md` for both/per-arm pitch slider control flow and pose sync/busy-gating behavior in WPF host.
- Newly added: review `2026-03-06_wpf_arm_pose_refinement_and_suggestion_optimization.md` for arm filtering/tuning, suggested arm preset automation, and native arm-pose update optimization.
- Newly added: review `2026-03-06_wpf_arm_pose_upperarm_only_hotfix.md` for upper-arm-only safety rollback, preset linked-bone pitch neutralization, and native static-skinning arm-pose scope reduction.
- Newly added: review `2026-03-06_arm_pose_policy_alignment_fix.md` for arm-pose gate policy mismatch root cause, XAV2 forced-skip removal, and runtime warning contract for policy-disabled scenarios.
- Newly added: review `2026-03-06_wpf_light_glass_editorial_ui_refresh.md` for WPF design token expansion, left-rail + workspace layout modernization, and render-only visibility sync updates.
- Newly added: review `2026-03-06_wpf_ui_v2_navigation_theme_diagnostics.md` for single-active rail navigation, diagnostics default-collapse policy, dual-theme runtime switch, and lightweight section transition optimization.
- Newly added: review `2026-03-06_wpf_ui_v3_shortcuts_focus_persistence.md` for HostCore-backed workspace restore, core keyboard shortcuts, nav keyboard traversal, and section-focused primary control routing.
- Newly added: review `2026-03-06_wpf_ui_v4_operation_hub_and_flow_timing.md` for Getting Started operation hub quick actions, block-reason consolidation, and first-broadcast timing telemetry (latest/median).
- Newly added: review `2026-03-06_arkit52_strict_full_support_pipeline.md` for strict ARKit52 channel binding policy, host/native coverage diagnostics, and non-fatal missing-channel warning behavior.
- Newly added: review `2026-03-06_arkit52_quality_refinement_hybrid_fallback.md` for strict-first hybrid fallback routing, ARKit quality scoring, per-group calibration tuning, and fallback warning telemetry.
- Newly added: review `2026-03-06_xav2_full_parity_contract_enforcement.md` for strict lilToon/Poiyomi parity contract enforcement, typed-v3 canonical migration policy, and hard-fail diagnostics behavior.
- Newly added: review `2026-03-06_xav2_standard_mtoon_strict_parity_expansion.md` for strict parity-family expansion to Standard/MToon, Unity/native shader-family inference alignment, and typed-v3 export policy widening.
- Newly added: review `2026-03-06_xav2_poiyomi_typed_material_parity_extension.md` for Poiyomi advanced typed-v3 extraction uplift, feature-flag parity alignment, and runtime typed parse regression coverage.
- Newly added: review `2026-03-06_xav2_typed_v4_and_depth_shadow_pass_slice.md` for typed-v4 canonical contract uplift, pass-flag driven depth/shadow scheduling, and fast-fallback-safe runtime diagnostics expansion.
- Newly added: review `2026-03-06_xav2_static_skinning_regression_and_safe_default_off.md` for XAV2 tube-shape regression triage, static skinning collapse guard contracts, and final safety-first default-off stabilization policy.
- Newly added: review `2026-03-06_xav2_pass_flags_and_tracking_strict_followup.md` for XAV2 all-pass-disabled fail-safe base-pass recovery, strict tracking wrapper skip-switch passthrough, and refreshed VRM GateK/GateL evidence fields.
- Newly added: review `2026-03-06_xav2_public_sdk_packaging_v100.md` for external SDK packaging baseline (`v1.0.0`), public contract docs rollout, legal/notice artifacts, UPM sample onboarding, and package release-gate coverage.
- Newly added: review `2026-03-06_shader_family_backend_split_liltoon_mtoon.md` for backend-owned liltoon/mtoon pass-graph dispatch, per-family pixel-shader split, safe common-backend fallback policy, and `NcAvatarInfo` backend diagnostics fields.
- Newly added: review `2026-03-06_shader_family_backend_split_poiyomi_standard.md` for poiyomi/standard backend dispatch expansion, 5-backend pass topology execution order, pipeline resource slot extension, and gate-verified safe fallback retention.
- Newly added: review `2026-03-06_tracking_threshold_ui_and_winui_failure_refinement.md` for WPF/WinUI parse/drop threshold UX completion, native submit error surfacing hardening, and WinUI repro failure hint expansion.
- Newly added: review `2026-03-06_tracking_hybrid_auto_input_watchdog_and_ui_hints.md` for HybridAuto default routing, no-input watchdog diagnostics, and per-source packet-age/hint surfacing in WPF/WinUI.
- Newly added: review `2026-03-06_tracking_full_contract_strict_gate_and_python_pinning.md` for strict tracking-contract gating (HostE2E/Fuzz/MediaPipe sanity), explicit MediaPipe Python pinning policy, and release dashboard candidate decision alignment.
- Newly added: review `2026-03-06_tracking_strict_runtime_venv_runbook.md` for project-local venv pinning standard, strict readiness wrapper execution flow, and failure-code triage runbook.
- Newly added: review `2026-03-06_tracking_strict_runtime_activation_and_gate_pass.md` for strict tracking runtime activation steps, fuzz-gate TFM compatibility fix, and tracking-contract PASS verification evidence.
- Newly added: review `2026-03-06_tracking_upper_body_webcam_autopose_wpf_winui.md` for upper-body auto-pose runtime merge (`manual + auto`), webcam-side shoulder/upper-arm extraction contract, tracking diagnostics extension, and WPF/WinUI operator toggle/status surfacing.
- Newly added: review `2026-03-06_host_perf_hotpath_and_metrics_contract_update.md` for frame-loop allocation reduction, metrics provenance/memory-sample contract expansion, and dist publish hygiene updates.
- Newly added: review `2026-03-06_vrm_mtoon_diagnostics_and_safe_material_fallback.md` for VRM matcap diagnostics precision, unresolved material-slot safe fallback behavior, and last-warning render-priority surfacing.
- Newly added: review `2026-03-06_vrm_mtoon_gate_hardening_and_stage1_baseline.md` for GateK/GateL policy, VRM unresolved warning-code normalization, and stage-1 MToon parity baseline telemetry.
- Newly added: review `2026-03-06_vrm_node_transform_skinning_and_preview_yaw_autofallback.md` for skinned node-transform unification, conflict-only fallback warning contract, and VRM preview yaw auto-fallback diagnostics.
- Newly added: review `2026-03-06_vrm_crash_guard_front_yaw_and_cull_hotfix.md` for invalid node-transform crash guard, VRM front-facing yaw default, and VRM no-cull render hotfix for transparent-looking outfit breakage.
