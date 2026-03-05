# Documentation Conventions

This file defines how documentation is written and maintained in this repository.

## 1. Language and Date Format

- Use Korean as the default narrative language.
- Keep code identifiers, APIs, and type names in original form.
- Use `YYYY-MM-DD` in report file names.
- Keep file names ASCII and lowercase with `_`.

## 2. Document Types

- `README.md`: onboarding and operational quick path.
- `CHANGELOG.md`: chronological change summary.
- `docs/formats/*.md`: format-level specs.
- `docs/reports/weekly/YYYY-Www/*.md`: canonical implementation/verification reports.
- `docs/reports/*_YYYY-MM-DD.md`: legacy redirect stubs only.
- `docs/reports/DOMAIN_INDEX.md`: domain-level navigation hub.
- `docs/reports/legacy-map.md`: old path to new path migration map.
- `docs/archive/`: historical docs and generated artifacts.

## 3. Report Placement and Naming

- New reports must be created under `docs/reports/weekly/YYYY-Www/`.
- Canonical report file name format:
  - `YYYY-MM-DD_<domain>_<topic>.md`
- Every weekly folder must include:
  - `INDEX.md`
  - `SUMMARY.md`
- Do not create new canonical reports at `docs/reports/` root.

## 4. Report Template

Every canonical report should use this structure:

1. Scope
2. Implemented Changes
3. Verification Summary
4. Known Risks or Limitations
5. Next Steps

Use concise conclusions and link evidence artifacts instead of pasting long logs.

## 5. Changelog Rules

Each changelog entry should include:

- `Summary`
- `Changed`
- `Verified` (optional if still in progress)

Detailed execution evidence belongs in canonical report files under `docs/reports/weekly/`.

## 6. Generated Report Retention

- Keep one latest snapshot per report family in `build/reports/`.
- Keep only meaningful milestone snapshots in `build/reports/`.
- Move historical artifacts to `docs/archive/build-reports/`.

## 7. Documentation Quality Gate

Run before merging documentation updates:

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\docs_quality_gate.ps1
```

Current checks:

- Links in `docs/INDEX.md` resolve.
- Weekly structure is valid (`INDEX.md`, `SUMMARY.md`, canonical reports).
- Legacy stubs map to canonical paths in `legacy-map.md`.
- Markdown files tracked by the gate are valid UTF-8.
