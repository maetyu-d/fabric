# 06 Bouncing Ball

Goal: turn one note into a decaying burst of repeated notes.

Patch: [examples/bouncing_ball.pulse](/Users/md/Downloads/plugin%20language/examples/bouncing_ball.pulse)

Concepts:

- `transform bounce` creates a gesture from each incoming note.
- `spacing start -> end` controls acceleration or deceleration.
- `velocity start -> end` controls the bounce fade.

What to try:

1. Increase the bounce count.
2. Reverse the spacing to make the gesture slow down instead of speed up.
3. Put `bounce` after an arp instead of after a simple note source.

Key idea:

Fabric can treat MIDI as a physical gesture, not just a stream of note events.
