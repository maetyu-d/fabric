# 01 First Pattern

Goal: make Fabric generate notes without any MIDI input.

Patch: [examples/pattern_machine.pulse](/Users/md/Downloads/plugin%20language/examples/pattern_machine.pulse)

Concepts:

- `clock` creates trigger pulses.
- `pattern` outputs a pitch sequence.
- `notes` turns pitch into MIDI.
- `midi out` sends the result out of the plugin.

What to try:

1. Change `every 1/16` to `every 1/8`.
2. Replace the note list with `C3 Eb3 G3 Bb3`.
3. Change the scale from `D dorian` to `C minor`.

Key idea:

Fabric patches are small signal graphs. Clock drives pattern, pattern drives notes, notes drive output.
