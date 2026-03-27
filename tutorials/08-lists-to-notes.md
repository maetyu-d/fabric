# 08 Lists To Notes

Goal: use list traversal to make a phrase.

Patch: [examples/lists_to_notes.pulse](/Users/md/Downloads/plugin%20language/examples/lists_to_notes.pulse)

Concepts:

- `shape lists` stores pitch, time, and gate lists.
- each list can advance for a different reason.
- `notes` turns the resulting streams into playable MIDI.

What to try:

1. Change the pitch list.
2. Make the time list much slower.
3. Experiment with interpolation on or off.

Key idea:

Lists are one of Fabric's main ways to build evolving, non-linear sequences.
