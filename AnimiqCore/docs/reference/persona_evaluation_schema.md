# Persona Evaluation Report Schema

Standard schema for persona-based platform evaluation reports.

## Purpose

- keep persona evaluations comparable across weeks
- ensure action-plan-first outputs
- align product, engineering, and release decision surfaces

## Required Fields

Each persona row must include:

- `Persona`: stable persona name/role
- `TopPain`: highest-impact current pain point
- `Evidence`: concrete source report/artifact/date
- `Impact`: short expected impact summary
- `UserValue` (1-5)
- `ImplComplexity` (1-5)
- `OpsRisk` (1-5)
- `CurrentCoverage` (0-2): `0=missing`, `1=partial`, `2=implemented`
- `Priority`: `P1`, `P2`, or `P3`
- `Action`: concrete next implementation or operational step
- `Owner`: responsible team/function
- `Window`: `Next sprint`, `+1 release`, or `+2 releases`
- `SuccessMetric`: measurable closure signal

## Optional Fields

- `Dependencies`: blocking prerequisite(s)
- `RollbackOrFallback`: mitigation path if action fails
- `RelatedRequirements`: requirement IDs (for example `R01`, `R06`)

## Priority Rules

Use one consistent scoring pass per report:

1. high `UserValue` + high `OpsRisk` + low `CurrentCoverage` -> raise priority
2. high `ImplComplexity` -> split into staged increments before scheduling
3. reserve `P1` only for next-sprint, execution-ready items

## Output Contract

Report must include:

1. summary and scope
2. persona evaluation table using required fields
3. consolidated backlog grouped by `P1/P2/P3`
4. explicit assumptions

This schema does not change product/runtime APIs; it defines report interoperability only.
