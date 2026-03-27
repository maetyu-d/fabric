# 07 Motion To Notes

Goal: derive control from incoming MIDI movement and turn it back into notes.

Patch: [examples/motion_to_notes.pulse](/Users/md/Downloads/plugin%20language/examples/motion_to_notes.pulse)

Concepts:

- `analyze motion` watches incoming MIDI activity.
- it can output pulses, gates, and a `speed` value.
- `notes` can use those outputs to make new MIDI.

What to try:

1. Play slowly, then quickly, and compare the result.
2. Change the quantized scale.
3. Use `motion1.odd` instead of `motion1.even`.

Key idea:

Fabric can turn performance behavior into control signals, not just pass MIDI through.
