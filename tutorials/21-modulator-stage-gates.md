# 21 Modulator Stage Gates

Goal: use stage-specific gate and end outputs as patch logic.

Patch: [examples/modulator_stage_gates.pulse](/Users/md/Downloads/plugin%20language/examples/modulator_stage_gates.pulse)

Concepts:

- `chN_sM_gate` tells you when a stage is active.
- `chN_sM_end` gives you an end-of-stage trigger.
- these can clock, gate, or switch other modules.

Key idea:

The modulator is not only for shaping values. It can become a logic source for the rest of the graph.
