# 05 MIDI Loop

Goal: capture incoming MIDI and replay it as a loop.

Patch: [examples/midi_loop.pulse](/Users/md/Downloads/plugin%20language/examples/midi_loop.pulse)

Concepts:

- `transform loop` records a phrase window.
- `playback on` replays the captured phrase.
- `quantize to` tightens replay timing.

What to try:

1. Play a short rhythmic phrase and let it repeat.
2. Change `capture 1/16` to `capture 1/8`.
3. Turn `overdub on` and layer new input.

Key idea:

Fabric can capture performance and turn it into material for the rest of the patch.
