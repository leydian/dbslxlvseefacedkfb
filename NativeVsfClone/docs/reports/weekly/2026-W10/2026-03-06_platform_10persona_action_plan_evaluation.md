# Platform 10-Persona Action Plan Evaluation (2026-03-06)

## Summary

This evaluation scores the full `NativeVsfClone` platform from 10 personas:

- 5 user personas
- 2 planner personas
- 3 developer personas

Output is action-plan first: each persona maps to concrete priorities, owners, and release windows.

Scope:

- Host apps (`WPF`, `WinUI`)
- `HostCore`
- `nativecore.dll`
- loader/runtime format paths (`.vrm/.xav2/.vsfavatar`)
- quality/release tooling and diagnostic contracts

Out of scope:

- net-new feature ideation outside current platform roadmap

## Scoring Model

- `UserValue` (1-5): end-user or stakeholder impact
- `ImplComplexity` (1-5): engineering effort/risk
- `OpsRisk` (1-5): runtime/release risk if missing
- `CurrentCoverage` (0-2): current implementation depth (`0=missing`, `1=partial`, `2=implemented`)

Priority interpretation:

- `P1`: next sprint
- `P2`: +1 release
- `P3`: +2 releases

## Persona Evaluation (Action-Oriented)

| Persona | Top Pain | Evidence (2026-03-05~06) | Score Snapshot | Priority | Action | Owner | Window | Success Metric |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| User 1 - Beginner Operator | first-run uncertainty and setup failure anxiety | onboarding KPI automation exists but first-run success still needs threshold gate (`onboarding_kpi_summary`) | Value 5 / Complexity 2 / Risk 5 / Coverage 1 | P1 | finish guided onboarding + preflight CTA wiring (`R01`) | Host UI + HostCore | Next sprint | `within_3min_success_rate_pct` trend up; first-run abort rate down |
| User 2 - Intermediate Creator | import path still feels split across formats and modes | unified import requirement remains partial (`R02`), sidecar controls exist but flow still fragmented | 5 / 3 / 4 / 1 | P1 | complete single import flow for `.vrm/.xav2/.vsfavatar` + async progress/cancel link (`R02+R13`) | Host UI + Loader | Next sprint | median import steps and retries decrease |
| User 3 - Live Broadcast Operator | fear of output drop or hidden mismatch during live run | output reconciliation/self-heal exists; high live risk remains core (`R06`) | 5 / 3 / 5 / 2 | P1 | tighten Spout/OSC mismatch alerting and bounded recovery visibility in UI | HostCore + Output | Next sprint | live output mismatch incidents and manual restart count down |
| User 4 - Unity Pipeline User | exporter/runtime contract confidence is not fully explicit in daily flow | compatibility matrix is documented; validator hardening still partial (`R15`) | 4 / 4 / 3 / 1 | P2 | extend Unity export validator edge cases and surface actionable pre-export warnings | Unity SDK + QA | +1 release | import failure rate from Unity exports decreases |
| User 5 - Recovery-Focused Power User | diagnostics are available but decision tree usage is inconsistent | one-click diagnostics bundle implemented (`R09`), troubleshooting sync still needs stronger UI tie (`R20`) | 4 / 2 / 4 / 2 | P2 | bind error code -> remediation -> repro command path directly in troubleshooting flow | HostCore + Docs | +1 release | mean time to self-recovery decreases |
| Planner 1 - Product Growth Planner | onboarding outcome cannot yet drive hard release gates | KPI summary generated but thresholding/governance pending | 5 / 3 / 4 / 1 | P1 | define and enforce onboarding KPI gate thresholds in release readiness | Product + Release Ops | Next sprint | release check includes KPI pass/fail with stable baseline |
| Planner 2 - Release Risk Planner | WinUI track status can still blur release messaging | WPF-first stable, WinUI diagnostics track and blocker evidence are active | 4 / 2 / 5 / 2 | P1 | publish dual-track release state (`WPF-only PASS`, `Full PASS`) with explicit customer-facing status copy | Release Ops + Docs | Next sprint | zero ambiguous release status incidents |
| Developer 1 - NativeCore C++ Owner | cross-layer contracts can drift without stricter policy checks | error code and diagnostics contracts are in place; continued guard needed | 4 / 3 / 4 / 2 | P2 | add stricter contract assertions for loader/runtime warning and error invariants | NativeCore + Tests | +1 release | contract drift regressions caught in CI before merge |
| Developer 2 - Host (.NET/WPF/WinUI) Owner | parity drift risk between shells remains a recurring cost | shared `HostUiPolicy` reduced drift, WinUI toolchain blocker still limits parity closure | 4 / 3 / 5 / 2 | P1 | continue WPF-first shared-policy expansion and isolate WinUI-specific blockers as diagnostics-only backlog | HostCore + Host Apps | Next sprint | parity bugs per sprint trend down |
| Developer 3 - SDK/QA Automation Owner | broad automation exists but signal-to-noise in gates can still improve | release board shows most tracks done; matrix and threshold policies still maturing | 3 / 2 / 4 / 2 | P2 | tighten gate dashboard with fail-closed thresholds and trend views for onboarding, render, and migration checks | QA Automation + Release Ops | +1 release | gate false-positive/false-negative rate down |

## Consolidated Priority Backlog

### P1 (Next sprint)

1. Guided onboarding + preflight CTA completion (`R01`).
2. Unified import flow completion with async progress/cancel (`R02+R13`).
3. Live output mismatch/recovery visibility hardening (`R06`).
4. KPI threshold-based release gate adoption (onboarding-focused).
5. WPF-first/WinUI-track release messaging contract finalization.
6. Host shell parity risk reduction via shared policy expansion.

### P2 (+1 release)

1. Unity export validator edge/negative-case expansion (`R15`).
2. Troubleshooting decision-tree and error remediation wiring (`R20`).
3. NativeCore contract assertions hardening.
4. Gate dashboard threshold/trend fail-closed refinement.

### P3 (+2 releases)

1. Remaining medium-term platform governance items from the R01-R20 board that are already low-risk/high-coverage.

## Assumptions

- Baseline source is the current repository state and weekly reports dated `2026-03-05` to `2026-03-06`.
- WPF is the primary production track; WinUI remains diagnostics-backed until blocker closure.
- This document is an execution planning artifact and does not change runtime/public API behavior directly.
