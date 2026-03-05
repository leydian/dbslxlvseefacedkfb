# Platform Persona Requirements Review (2026-03-05)

## Summary

This review defines 20 platform requirements for `NativeVsfClone` and evaluates each item across five personas:

- User Persona A: Beginner operator
- User Persona B: Intermediate creator
- User Persona C: Live broadcast operator
- Planner Persona: Product planner
- Developer Persona: Implementation owner

Scoring model:

- `UserValue` (1-5): user-facing impact
- `ImplComplexity` (1-5): implementation effort/risk
- `OpsRisk` (1-5): runtime/release risk if missing

Priority rule:

- prioritize items with high `UserValue` and high `OpsRisk`
- split complex items into MVP increments when needed

## Top 20 Cross-Persona Review

| ID | Requirement | ScopeArea | Beginner | Intermediate | LiveOps | Planner | Developer | UserValue | ImplComplexity | OpsRisk | PriorityTier | TargetReleaseWindow |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| R01 | Guided onboarding + environment preflight wizard | Host/Toolchain | setup anxiety reduced | fewer setup retries | fewer show-day surprises | adoption funnel improves | clear check contracts | 5 | 3 | 5 | Immediate | Next sprint |
| R02 | Unified avatar import wizard for `.vrm/.vxavatar/.vxa2/.xav2/.vsfavatar` | Loader/Host | one path to success | faster switching | fast fallback in emergencies | lower support cost | shared loader facade reuse | 5 | 4 | 4 | Short-term | +1 release |
| R03 | Error taxonomy + user-friendly remediation messages | Core/Host | understands failure cause | less trial-and-error | faster incident recovery | measurable failure classes | deterministic error mapping | 5 | 3 | 5 | Immediate | Next sprint |
| R04 | Preset profiles (quality/perf/stability) with safe defaults | Render/UX | no tuning burden | quick baseline tuning | stable frame budget | clearer positioning tiers | bounded config surface | 4 | 3 | 4 | Short-term | +1 release |
| R05 | Auto quality guardrails (FPS floor, resize debounce, DPI sync) | Render/HostCore | smoother first run | less manual tuning | protects stream continuity | improves NPS/retention | existing hooks extendable | 5 | 3 | 5 | Short-term | +1 release |
| R06 | Output reconciliation and bounded self-heal for Spout/OSC | Output/HostCore | fewer silent failures | fewer manual restarts | critical for live stability | reduces outage risk | state machine complexity manageable | 5 | 4 | 5 | Immediate | Next sprint |
| R07 | Strict operation state machine (`Initialize->Load->Start`) and invalid action blocking | HostCore/UI | clear workflow learning | prevents accidental misuse | lower operator error rate | policy consistency | parity logic centralized | 5 | 2 | 5 | Immediate | Next sprint |
| R08 | Crash-safe autosave for session and render/output presets | Host/Storage | safe recovery after crash | preserves work context | minimizes downtime | lowers support ticket load | simple local persistence | 4 | 3 | 4 | Short-term | +1 release |
| R09 | One-click diagnostics bundle (logs + manifest + env snapshot) | Tooling/Support | easy to ask help | reproducible bug reports | quick post-incident handoff | triage cycle shortened | artifact schema required | 5 | 3 | 5 | Immediate | Next sprint |
| R10 | Runtime performance HUD + rolling metrics export | Runtime/UX | basic health visibility | optimization feedback | detects degradation early | KPI visibility | low-risk additive feature | 4 | 3 | 4 | Short-term | +1 release |
| R11 | Format compatibility matrix and fallback policy docs in product UI | Loader/Docs | expectation clarity | picks right format faster | predictable fallback choice | fewer ambiguous requirements | formal policy reduces drift | 4 | 2 | 4 | Medium-term | +2 releases |
| R12 | Sidecar parser management UI (mode, timeout, path, strict fallback) | VSFAvatar/Host | avoids env var confusion | controllable parsing behavior | safer incident toggles | feature discoverability improves | settings validation needed | 4 | 3 | 4 | Short-term | +1 release |
| R13 | Async load with progress, cancel, and timeout visibility | Loader/UI | feedback during long load | multitask-friendly | avoids dead-air during live prep | better UX metrics | threading/cancel safety work | 4 | 4 | 4 | Medium-term | +2 releases |
| R14 | Batch validation CLI for avatar packs with gate-style summary | Tooling/CI | optional for advanced users | efficient library cleanup | pre-show risk reduction | quality baseline governance | reuses gate scripts | 3 | 3 | 4 | Medium-term | +2 releases |
| R15 | Unity XAV2 export validator and compatibility checker | Unity SDK | fewer bad exports | faster iteration loop | fewer runtime import failures | strengthens ecosystem | editor/runtime contract tests | 4 | 4 | 3 | Medium-term | +2 releases |
| R16 | Release gate dashboard (WPF/WinUI/native/format tracks) | CI/Release | indirect benefit | confidence in updates | lower regression exposure | release go/no-go clarity | aggregates existing reports | 3 | 3 | 4 | Medium-term | +2 releases |
| R17 | WinUI blocker fallback policy with user-visible track status | Host/Release | knows supported path | avoids blocked path time loss | predictable contingency | roadmap transparency | maintain two-track logic | 4 | 2 | 4 | Short-term | +1 release |
| R18 | Public API and package version contract (nativecore/HostCore/Unity SDK) | API/Platform | fewer break surprises | stable integrations | reliable update windows | dependency planning improves | semantic version discipline | 3 | 3 | 4 | Medium-term | +2 releases |
| R19 | Telemetry and privacy controls (opt-in, export, redact) | Ops/Compliance | trust by default | choose data sharing level | safer external support sharing | compliance readiness | policy + plumbing needed | 3 | 4 | 3 | Medium-term | +2 releases |
| R20 | Quickstart and troubleshooting decision tree synchronized with UI copy | Docs/UX | quicker self-resolution | fewer context switches | runbook-like operation aid | support cost reduction | docs drift management needed | 4 | 2 | 3 | Short-term | +1 release |

## Immediate Top 5 (Next Sprint)

1. `R01` Guided onboarding + preflight wizard
2. `R03` Error taxonomy + remediation messages
3. `R06` Output reconciliation + bounded self-heal
4. `R07` Strict operation state machine enforcement
5. `R09` One-click diagnostics bundle

Acceptance signal for immediate set:

- first-run to successful avatar load/output path time decreases
- incident triage time decreases with reproducible artifacts
- output mismatch and invalid operation regressions trend downward

## Assumptions and Defaults

- Scope includes full platform (`NativeCore`, loader stack, Host apps, Unity XAV2 SDK, release/ops tooling).
- Persona set is fixed to beginner/intermediate/live operator plus planner/developer.
- Prioritization is based on current repository state (WPF-first policy and known WinUI toolchain blocker).
