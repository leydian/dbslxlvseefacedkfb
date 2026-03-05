# Documentation Index

Entry point for `NativeVsfClone` documentation.

## Core

- [README.md](/D:/dbslxlvseefacedkfb/NativeVsfClone/README.md): project overview, build/run flow, current status
- [CHANGELOG.md](/D:/dbslxlvseefacedkfb/NativeVsfClone/CHANGELOG.md): chronological implementation changes
- [CONTRIBUTING_DOCS.md](/D:/dbslxlvseefacedkfb/NativeVsfClone/docs/CONTRIBUTING_DOCS.md): documentation maintenance rules

## Format Specs

- [vxa2.md](/D:/dbslxlvseefacedkfb/NativeVsfClone/docs/formats/vxa2.md): `.vxa2` format spec
- [xav2.md](/D:/dbslxlvseefacedkfb/NativeVsfClone/docs/formats/xav2.md): `.xav2` format spec

## Reports

- [xav2_front_view_and_material_alignment_2026-03-05.md](/D:/dbslxlvseefacedkfb/NativeVsfClone/docs/reports/xav2_front_view_and_material_alignment_2026-03-05.md): XAV2 front-view default orientation fix plus material/texture alignment and native shader-param interpretation updates
- [xav2_native_uv_offset_fix_2026-03-05.md](/D:/dbslxlvseefacedkfb/NativeVsfClone/docs/reports/xav2_native_uv_offset_fix_2026-03-05.md): native XAV2 render-path UV decode fix for expanded-stride Unity payloads to resolve scrambled texture mapping
- [xav2_liltoon_texture_export_fallback_2026-03-05.md](/D:/dbslxlvseefacedkfb/NativeVsfClone/docs/reports/xav2_liltoon_texture_export_fallback_2026-03-05.md): Unity XAV2 exporter texture-path hardening for lilToon/non-readable textures via RenderTexture fallback and expanded base map property probe
- [session_change_summary_2026-03-05.md](/D:/dbslxlvseefacedkfb/NativeVsfClone/docs/reports/session_change_summary_2026-03-05.md): consolidated latest-session rollup covering WPF crash hotfix, Unity XAV2 relaxed export menu, avatar extension policy trim, and current host load-failure behavior
- [xav2_relaxed_export_menu_2026-03-05.md](/D:/dbslxlvseefacedkfb/NativeVsfClone/docs/reports/xav2_relaxed_export_menu_2026-03-05.md): Unity XAV2 strict-policy bypass path addition via opt-in relaxed export menu with mode-aware diagnostics/logging
- [r01_r20_top5_execution_and_winui_sdk_contract_2026-03-05.md](/D:/dbslxlvseefacedkfb/NativeVsfClone/docs/reports/r01_r20_top5_execution_and_winui_sdk_contract_2026-03-05.md): Top5 (`R01/R02/R03/R05/R13`) implementation closure with HostCore contract upgrades, WPF/WinUI parity updates, and WinUI SDK-contract diagnostics execution
- [r01_r20_phase2_winui_parity_execution_2026-03-05.md](/D:/dbslxlvseefacedkfb/NativeVsfClone/docs/reports/r01_r20_phase2_winui_parity_execution_2026-03-05.md): phase-2 execution report for WinUI parity lift (Platform Ops, async-load progress/cancel, guides/track status) with current XAML compiler blocker evidence
- [r01_r20_detailed_plan_and_execution_2026-03-05.md](/D:/dbslxlvseefacedkfb/NativeVsfClone/docs/reports/r01_r20_detailed_plan_and_execution_2026-03-05.md): detailed execution plan plus implementation result for the latest R01-R20 follow-up round (Top5 hardening)
- [platform_persona_requirements_reassessment_2026-03-05.md](/D:/dbslxlvseefacedkfb/NativeVsfClone/docs/reports/platform_persona_requirements_reassessment_2026-03-05.md): latest-state reassessment of 20 platform requirements with 3 user personas + planner/developer, including coverage tags and updated Top 5 execution priorities
- [r01_r20_mvp_implementation_round_2026-03-05.md](/D:/dbslxlvseefacedkfb/NativeVsfClone/docs/reports/r01_r20_mvp_implementation_round_2026-03-05.md): detailed implementation rollup for R01-R20 MVP feature code across HostCore/WPF/tools with verification and coverage snapshot
- [platform_persona_requirements_review_2026-03-05.md](/D:/dbslxlvseefacedkfb/NativeVsfClone/docs/reports/platform_persona_requirements_review_2026-03-05.md): platform-wide 20-item requirement review with 3 user personas + planner/developer cross-perspective scoring and immediate Top 5 priorities
- [ui_host_runtime_integration_2026-03-02.md](/D:/dbslxlvseefacedkfb/NativeVsfClone/docs/reports/ui_host_runtime_integration_2026-03-02.md): UI host foundation + native render/output integration report
- [ui_wpf_refresh_throttle_2026-03-05.md](/D:/dbslxlvseefacedkfb/NativeVsfClone/docs/reports/ui_wpf_refresh_throttle_2026-03-05.md): WPF operation-flow relayout + 60Hz render / 10Hz UI refresh throttle update
- [wpf_ui_smoke_and_perf_2026-03-05.md](/D:/dbslxlvseefacedkfb/NativeVsfClone/docs/reports/wpf_ui_smoke_and_perf_2026-03-05.md): WPF smoke/performance evidence with WPF-only publish/gate/baseline verification snapshot
- [wpf_verification_roundup_2026-03-05.md](/D:/dbslxlvseefacedkfb/NativeVsfClone/docs/reports/wpf_verification_roundup_2026-03-05.md): consolidated roundup of commands, outcomes, artifacts, and deferred manual checks for the latest WPF-first verification round
- [wpf_operational_reliability_loop_and_output_sync_2026-03-05.md](/D:/dbslxlvseefacedkfb/NativeVsfClone/docs/reports/wpf_operational_reliability_loop_and_output_sync_2026-03-05.md): HostCore output-state mismatch reconciliation + bounded auto-recovery and WPF reliability loop gate script introduction
- [workspace_change_rollup_2026-03-05.md](/D:/dbslxlvseefacedkfb/NativeVsfClone/docs/reports/workspace_change_rollup_2026-03-05.md): consolidated rollup of current workspace changes across VRM runtime/API expansion, host reliability hardening, quality gate updates, and documentation sync
- [ui_persona_consolidation_and_policy_refactor_2026-03-05.md](/D:/dbslxlvseefacedkfb/NativeVsfClone/docs/reports/ui_persona_consolidation_and_policy_refactor_2026-03-05.md): persona-based UI consolidation (single action source), shared HostCore UI policy extraction, WinUI readability/min-size guard, and validation summary
- [winui_ui_refresh_followup_ticket_2026-03-05.md](/D:/dbslxlvseefacedkfb/NativeVsfClone/docs/reports/winui_ui_refresh_followup_ticket_2026-03-05.md): WinUI parity follow-up ticket for refresh-throttle model
- [winui_ui_refresh_throttle_parity_2026-03-05.md](/D:/dbslxlvseefacedkfb/NativeVsfClone/docs/reports/winui_ui_refresh_throttle_parity_2026-03-05.md): WinUI parity implementation details (10Hz UI + dirty-flag + log-tab-aware refresh) and current blocker status
- [host_wpf_first_transition_2026-03-05.md](/D:/dbslxlvseefacedkfb/NativeVsfClone/docs/reports/host_wpf_first_transition_2026-03-05.md): WPF-first host policy transition, CI split, and latest verification snapshot
- [host_blocker_status_board_2026-03-05.md](/D:/dbslxlvseefacedkfb/NativeVsfClone/docs/reports/host_blocker_status_board_2026-03-05.md): unified open/closed/next-actions tracker for WinUI blocker and WPF smoke stability work
- [host_winui_diag_profile_and_wpf_smoke_2026-03-05.md](/D:/dbslxlvseefacedkfb/NativeVsfClone/docs/reports/host_winui_diag_profile_and_wpf_smoke_2026-03-05.md): detailed implementation report for WinUI diagnostics profile expansion, WPF launch smoke automation, and CI matrix evidence contract
- [host_change_rollup_2026-03-05.md](/D:/dbslxlvseefacedkfb/NativeVsfClone/docs/reports/host_change_rollup_2026-03-05.md): consolidated summary of diagnostics schema hardening, re-validation, and current WinUI blocker state
- [host_diagnostics_schema_update_2026-03-05.md](/D:/dbslxlvseefacedkfb/NativeVsfClone/docs/reports/host_diagnostics_schema_update_2026-03-05.md): WinUI diagnostic manifest schema expansion (`preflight_probe`) and failure-class precedence update
- [host_execution_round_2026-03-05.md](/D:/dbslxlvseefacedkfb/NativeVsfClone/docs/reports/host_execution_round_2026-03-05.md): baseline/host/gate re-run with SDK8 remediation and current WinUI blocker status
- [host_gate_execution_round_2026-03-04.md](/D:/dbslxlvseefacedkfb/NativeVsfClone/docs/reports/host_gate_execution_round_2026-03-04.md): gate re-validation + host publish execution round with WinUI blocker diagnostics hardening
- [host_plan_execution_update_2026-03-04.md](/D:/dbslxlvseefacedkfb/NativeVsfClone/docs/reports/host_plan_execution_update_2026-03-04.md): detailed implementation summary for preflight fail-fast, HostTrack auto-resolution, and CI integration
- [host_runtime_parity_smoke_checklist_2026-03-04.md](/D:/dbslxlvseefacedkfb/NativeVsfClone/docs/reports/host_runtime_parity_smoke_checklist_2026-03-04.md): post-publish WPF/WinUI manual runtime parity smoke checklist
- [ui_host_operation_redesign_2026-03-03.md](/D:/dbslxlvseefacedkfb/NativeVsfClone/docs/reports/ui_host_operation_redesign_2026-03-03.md): WPF/WinUI operation-focused host UI redesign and shared HostCore state controller report
- [ui_host_auto_quality_2026-03-03.md](/D:/dbslxlvseefacedkfb/NativeVsfClone/docs/reports/ui_host_auto_quality_2026-03-03.md): DPI-aware render sizing, resize debounce, and Spout auto-reconfigure quality pass report
- [ui_host_validation_busy_and_winui_diag_2026-03-03.md](/D:/dbslxlvseefacedkfb/NativeVsfClone/docs/reports/ui_host_validation_busy_and_winui_diag_2026-03-03.md): host input validation, busy-state gating, and WinUI diagnostic environment snapshot update
- [ui_render_busy_gating_parity_2026-03-03.md](/D:/dbslxlvseefacedkfb/NativeVsfClone/docs/reports/ui_render_busy_gating_parity_2026-03-03.md): render/preset busy-state gating follow-up and WPF/WinUI parity alignment report
- [host_stabilization_round_2026-03-03.md](/D:/dbslxlvseefacedkfb/NativeVsfClone/docs/reports/host_stabilization_round_2026-03-03.md): host publish/build failure-path hardening and WinUI diagnostics validation
- [winui_xaml_diagnostics_artifacts_2026-03-03.md](/D:/dbslxlvseefacedkfb/NativeVsfClone/docs/reports/winui_xaml_diagnostics_artifacts_2026-03-03.md): WinUI XAML compile failure artifact map and troubleshooting order
- [ui_render_advanced_controls_2026-03-03.md](/D:/dbslxlvseefacedkfb/NativeVsfClone/docs/reports/ui_render_advanced_controls_2026-03-03.md): WPF/WinUI advanced render composition controls and local preset persistence update report
- [host_exe_publish_2026-03-02.md](/D:/dbslxlvseefacedkfb/NativeVsfClone/docs/reports/host_exe_publish_2026-03-02.md): WPF/WinUI self-contained EXE publish pipeline report
- [vxavatar_mvp_update_2026-03-02.md](/D:/dbslxlvseefacedkfb/NativeVsfClone/docs/reports/vxavatar_mvp_update_2026-03-02.md): `.vxavatar` MVP update report
- [vxa2_tlv_update_2026-03-02.md](/D:/dbslxlvseefacedkfb/NativeVsfClone/docs/reports/vxa2_tlv_update_2026-03-02.md): `.vxa2` TLV decode MVP report
- [vrm_runtime_slice_2026-03-03.md](/D:/dbslxlvseefacedkfb/NativeVsfClone/docs/reports/vrm_runtime_slice_2026-03-03.md): `.vrm` runtime-ready slice + minimal render path report
- [vxavatar_gate_harness_2026-03-02.md](/D:/dbslxlvseefacedkfb/NativeVsfClone/docs/reports/vxavatar_gate_harness_2026-03-02.md): VXAvatar/VXA2/XAV2 quality gate harness report
- [xav2_signature_gate_and_vsf_fallback_2026-03-03.md](/D:/dbslxlvseefacedkfb/NativeVsfClone/docs/reports/xav2_signature_gate_and_vsf_fallback_2026-03-03.md): XAV2 payload expansion + signature routing + VSFAvatar fallback update report
- [vxavatar_gate_ci_expansion_2026-03-03.md](/D:/dbslxlvseefacedkfb/NativeVsfClone/docs/reports/vxavatar_gate_ci_expansion_2026-03-03.md): VXAvatar/VXA2 quick/full gate + CI expansion report
- [vrm_quality_pass_2026-03-03.md](/D:/dbslxlvseefacedkfb/NativeVsfClone/docs/reports/vrm_quality_pass_2026-03-03.md): VRM material/texture quality pass + 5-sample gate report
- [xav2_sdk_bootstrap_2026-03-02.md](/D:/dbslxlvseefacedkfb/NativeVsfClone/docs/reports/xav2_sdk_bootstrap_2026-03-02.md): XAV2 loader/converter/Unity SDK scaffold bootstrap report
- [xav2_avatarroot_export_2026-03-02.md](/D:/dbslxlvseefacedkfb/NativeVsfClone/docs/reports/xav2_avatarroot_export_2026-03-02.md): Unity AvatarRoot direct export + skin/blendshape section extension report
- [xav2_sdk_diagnostics_and_gate_expansion_2026-03-03.md](/D:/dbslxlvseefacedkfb/NativeVsfClone/docs/reports/xav2_sdk_diagnostics_and_gate_expansion_2026-03-03.md): Unity XAV2 runtime diagnostics API hardening + VRM-derived fixed XAV2 gate expansion report
- [xav2_tryload_test_and_deterministic_gate_2026-03-03.md](/D:/dbslxlvseefacedkfb/NativeVsfClone/docs/reports/xav2_tryload_test_and_deterministic_gate_2026-03-03.md): XAV2 strict option path, runtime test suite, and deterministic VRM allowlist gate report
- [xav2_native_unknown_section_parity_2026-03-03.md](/D:/dbslxlvseefacedkfb/NativeVsfClone/docs/reports/xav2_native_unknown_section_parity_2026-03-03.md): native XAV2 unknown-section policy parity and warning-code diagnostics expansion report
- [xav2_policy_gateH_ci_2026-03-03.md](/D:/dbslxlvseefacedkfb/NativeVsfClone/docs/reports/xav2_policy_gateH_ci_2026-03-03.md): Gate H expansion for XAV2 policy contract and CI gate coverage
- [xav2_unity_2021_3_18f1_support_and_gate_2026-03-05.md](/D:/dbslxlvseefacedkfb/NativeVsfClone/docs/reports/xav2_unity_2021_3_18f1_support_and_gate_2026-03-05.md): Unity SDK support floor expansion (`2021.3.18f1+`) and CI gate automation report
- [nativecore_render_quality_api_sync_2026-03-02.md](/D:/dbslxlvseefacedkfb/NativeVsfClone/docs/reports/nativecore_render_quality_api_sync_2026-03-02.md): nativecore render quality public C API sync report
- [ui_render_benchmark_plan_2026-03-02.md](/D:/dbslxlvseefacedkfb/NativeVsfClone/docs/reports/ui_render_benchmark_plan_2026-03-02.md): host render UI benchmark KPI and acceptance scenario plan
- [vsfavatar_gate_harness_2026-03-03.md](/D:/dbslxlvseefacedkfb/NativeVsfClone/docs/reports/vsfavatar_gate_harness_2026-03-03.md): VSFAvatar quality gate harness report
- [vsfavatar_stage_lift_diff_2026-03-03.md](/D:/dbslxlvseefacedkfb/NativeVsfClone/docs/reports/vsfavatar_stage_lift_diff_2026-03-03.md): VSFAvatar stage transition diff report
- [vsfavatar_serialized_gateD_update_2026-03-03.md](/D:/dbslxlvseefacedkfb/NativeVsfClone/docs/reports/vsfavatar_serialized_gateD_update_2026-03-03.md): VSFAvatar serialized candidate expansion + GateD strict update report
- [vsfavatar_gateD_lzma_pass_2026-03-03.md](/D:/dbslxlvseefacedkfb/NativeVsfClone/docs/reports/vsfavatar_gateD_lzma_pass_2026-03-03.md): VSFAvatar UnityFS LZMA decode integration + GateD pass report
- [vsfavatar_serialized_bottleneck_followup_2026-03-03.md](/D:/dbslxlvseefacedkfb/NativeVsfClone/docs/reports/vsfavatar_serialized_bottleneck_followup_2026-03-03.md): VSFAvatar serialized bottleneck follow-up with 4/4 fixed-set complete result

## Generated Reports

- [build/reports/README.md](/D:/dbslxlvseefacedkfb/NativeVsfClone/build/reports/README.md): generated report retention/archive policy
- `build/reports/`: latest report and meaningful milestone snapshots
- `docs/archive/build-reports/`: archived generated report history

## Archive

- `docs/archive/`: historical docs and generated outputs that are no longer primary references
