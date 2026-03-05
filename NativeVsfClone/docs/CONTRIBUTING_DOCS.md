# Documentation Conventions

This file defines how documentation is written and maintained in this repository.

## 1. Language and Date Format

- Use clear Korean or English for narrative text. Keep terms consistent within one document.
- Keep code identifiers and API/type names in original form.
- Always use `YYYY-MM-DD` for dates in titles and report filenames.

## 2. Document Types

- `README.md`: onboarding and operational quick path.
- `CHANGELOG.md`: chronological change summary.
- `docs/formats/*.md`: format-level specs.
- `docs/reports/*.md`: implementation and verification reports.
- `docs/archive/`: no-longer-primary historical docs.

## 3. Report Template

Every `docs/reports/*.md` document should use this structure:

1. Scope
2. Implemented Changes
3. Verification Summary
4. Known Risks or Limitations
5. Next Steps

Use concise conclusions and link evidence artifacts instead of pasting long logs.

## 4. Changelog Rules

Each changelog entry should include:

- `Summary`
- `Changed`
- `Verified` (optional if still in progress)

Detailed execution evidence belongs in `docs/reports/*.md`. Keep changelog entries summary-level.

## 5. Generated Report Retention

- Keep one latest snapshot per report family in `build/reports/`.
- Keep only meaningful milestone snapshots in `build/reports/`.
- Move historical artifacts to `docs/archive/build-reports/`.

## 6. Documentation Quality Gate

Run before merging documentation updates:

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\docs_quality_gate.ps1
```

Current checks:

- `docs/INDEX.md` covers all `docs/reports/*.md` files.
- Markdown links resolve to existing local files.
- docs-related markdown files are valid UTF-8.
