# Docs IA Overhaul + Quality Gate Automation (2026-03-06)

## Scope

- Reorganize documentation information architecture for internal developer-first navigation.
- Enforce canonical/legacy separation:
  - canonical reports under `docs/reports/weekly/YYYY-Www/`
  - root `docs/reports/*_YYYY-MM-DD.md` as legacy redirect stubs
- Expand docs quality gate from index-only checks to full `docs/**/*.md` integrity checks.
- Keep backward compatibility for legacy report paths through automatic map/stub synchronization.

Out of scope:

- product/runtime behavior changes
- non-document release gate policies unrelated to docs integrity

## Implemented Changes

### 1) IA and navigation updates

- Moved reusable schema out of legacy-stub root:
  - from `docs/reports/persona_evaluation_schema.md`
  - to `docs/reference/persona_evaluation_schema.md`
- Added `docs/reference/INDEX.md` as reference-doc hub.
- Updated `docs/INDEX.md` to expose `Reference Docs` entrypoint and new schema path.
- Updated `docs/CONTRIBUTING_DOCS.md`:
  - formalized `docs/reference/*.md` document type
  - clarified root reports as auto-generated legacy stubs
  - documented new sync command for legacy stubs
  - updated quality gate checklist to global-link and weekly-index consistency checks

### 2) Legacy compatibility automation

- Added `tools/docs_sync_legacy_stubs.ps1`.
- Script behavior:
  - scans `docs/reports/*_YYYY-MM-DD.md` legacy files
  - normalizes each file to a strict redirect stub contract:
    - `New location`
    - `Archived original snapshot`
  - backfills missing canonical weekly files when needed
  - backfills missing archive snapshots when needed
  - rebuilds `docs/reports/legacy-map.md` from synchronized state
- Sync result:
  - `legacy-map.md` expanded and normalized to `119` mappings
  - legacy-map missing entries resolved
  - legacy-stub target-missing cases resolved

### 3) Quality gate hardening

- Reworked `tools/docs_quality_gate.ps1`:
  - validates markdown links across all `docs/**/*.md` files
  - keeps absolute-path link block policy
  - keeps UTF-8 checks
  - validates weekly `INDEX.md` and `SUMMARY.md` presence
  - adds weekly index consistency:
    - all canonical weekly reports must appear in weekly `INDEX.md`
    - weekly `INDEX.md` must not contain stale report links
  - narrows legacy stub 대상으로 `*_YYYY-MM-DD.md` pattern only
    (prevents non-stub reference docs from false classification)

### 4) Weekly index automation

- Added `tools/docs_rebuild_weekly_indexes.ps1`.
- Script behavior:
  - rebuilds each week folder `INDEX.md` from actual canonical report files
  - rebuilds `docs/reports/weekly/INDEX.md` with current week report counts
- Applied to current repo:
  - `docs/reports/weekly/2026-W10/INDEX.md` regenerated
  - `docs/reports/weekly/INDEX.md` regenerated (`2026-W10: 203 reports`)

## Verification Summary

Executed:

1. `powershell -ExecutionPolicy Bypass -File tools/docs_sync_legacy_stubs.ps1`
2. `powershell -ExecutionPolicy Bypass -File tools/docs_rebuild_weekly_indexes.ps1`
3. `powershell -ExecutionPolicy Bypass -File tools/docs_quality_gate.ps1`

Final gate status: `PASS`

- broken links in `docs/INDEX.md`: `0`
- broken markdown links in `docs/`: `0`
- missing weekly `INDEX.md`/`SUMMARY.md`: `0`
- invalid canonical names: `0`
- weekly reports missing from weekly index: `0`
- weekly index stale entries: `0`
- legacy stubs missing map: `0`
- legacy stubs with invalid target: `0`
- legacy map missing new target: `0`
- UTF-8 invalid files: `0`

## Known Risks or Limitations

- Legacy stub synchronization currently targets date-suffixed root report files by naming pattern.
- Existing external consumers that parse legacy content body (not redirect contract) may need adaptation.
- Weekly index rebuild is deterministic but currently manual (scripted command); CI wiring is recommended.

## Next Steps

1. Add CI job step to run:
   - `tools/docs_sync_legacy_stubs.ps1`
   - `tools/docs_rebuild_weekly_indexes.ps1`
   - `tools/docs_quality_gate.ps1`
2. Make docs gate required for PR merge.
3. Periodically trim legacy root stubs if long-term URL compatibility window is later reduced.
