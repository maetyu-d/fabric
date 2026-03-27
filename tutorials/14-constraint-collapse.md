# 14 Constraint Collapse

Goal: generate lines that keep reforming when their own rules break.

Patch: [examples/constraint_collapse.pulse](/Users/md/Downloads/plugin%20language/examples/constraint_collapse.pulse)

Concepts:

- `collapse` works with named rule sets.
- `ruleset ... from ...` gives you inheritance.
- `on collapse` and `on section` let states mutate over time.
- collapse patches stay readable with direct phrases like `on collapse ...`, `start verse`, and `section chorus ...`.

What to try:

1. Make the `broken` state more extreme.
2. Change the section plan.
3. Change the recovery target from tonic to dominant.

Key idea:

Collapse is one of Fabric's most distinctive systems: composition by failure, reform, and state change.
