# 02 Live Quantize And Arp

Goal: feed live MIDI into Fabric, constrain it to a scale, and drive an arpeggiator.

Patch: [examples/tutorial_live_quantize_arp.pulse](/Users/md/Downloads/plugin%20language/examples/tutorial_live_quantize_arp.pulse)

Concepts:

- `midi in` receives your notes.
- `quantize` snaps them to a scale.
- `arp` turns held notes into a repeating pattern.
- the clock only drives the arp trigger, not the input itself.

What to try:

1. Hold a simple triad and listen to the arp.
2. Change `gate 68%` to `40%`.
3. Change the scale to `A minor`.

Key idea:

This is the basic "live player into musical processor" Fabric pattern.
