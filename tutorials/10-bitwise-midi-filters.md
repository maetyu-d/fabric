# 10 Bitwise MIDI Filters

Goal: do low-level operations on specific MIDI lanes.

Patch: [examples/tutorial_filtered_bits.pulse](/Users/md/Downloads/plugin%20language/examples/tutorial_filtered_bits.pulse)

Concepts:

- `transform bits` can target note, velocity, controller data, and more.
- filters like `status`, `note`, `velocity`, `cc`, `except cc`, and `channel` narrow the scope.
- this lets you glitch one part of the MIDI stream while leaving the rest alone.

What to try:

1. Change `and` to `xor`.
2. Narrow the note range.
3. Switch from note velocity processing to CC processing.

Key idea:

Fabric has both a musical layer and a raw MIDI data layer.
