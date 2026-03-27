# 15 Complex Modulator

Goal: use Fabric's dedicated modulator as an envelope, sequencer, timing source, and control router.

Patch: [examples/modulated_modulator.pulse](/Users/md/Downloads/plugin%20language/examples/modulated_modulator.pulse)

Concepts:

- `shape modulator` can run multiple channels at once.
- per-stage inputs let one signal modulate another stage's level, time, or curve.
- the plugin UI now shows channel levels and active stages in the modulator inspector.
- stage outputs like `ch1_s2_gate` and `ch1_s2_end` are available for more advanced routing.
- stage lines can be written in a more human way, for example:
  - `stage 2 ch 1 to 0.85 for 110ms curve smooth`

What to try:

1. Change one stage time and listen to how note duration changes.
2. Add `overlap on` and stage `overlap` times.
3. Route a stage gate or stage end output into another module.

Key idea:

This is Fabric's main deep-control module. It is the bridge between modulation, timing, and composition.
