# 03 Random Walk

Goal: generate melody with controlled randomness.

Patch: [examples/random_machine.pulse](/Users/md/Downloads/plugin%20language/examples/random_machine.pulse)

Concepts:

- `random` can choose notes from a pool.
- `mode walk` keeps the melody moving locally instead of jumping anywhere.
- `avoid repeat`, `max step`, and `bias center` shape the result.

What to try:

1. Lower `pass` to make the line sparser.
2. Increase `max step` for wider leaps.
3. Change the note pool to a pentatonic set.

Key idea:

Random in Fabric does not have to mean chaotic. It can be directed and musical.
