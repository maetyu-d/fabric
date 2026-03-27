# 12 Warped Pitch Space

Goal: bend pitch movement through a non-linear internal space.

Patch: [examples/warped_pitch_space.pulse](/Users/md/Downloads/plugin%20language/examples/warped_pitch_space.pulse)

Concepts:

- `transform warp` can fold one pitch area near another.
- `wormhole` rules introduce controlled jumps.
- the warped pitch stream still ends up as playable MIDI through `notes`.

What to try:

1. Change the fold destination.
2. Change the wormhole interval range.
3. Lower `amount` for subtler motion.

Key idea:

Fabric does not have to think of pitch as a straight line.
