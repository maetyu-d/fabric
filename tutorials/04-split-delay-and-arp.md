# 04 Split Delay And Arp

Goal: process one MIDI input stream in parallel.

Patch: [examples/split_delay_arp.pulse](/Users/md/Downloads/plugin%20language/examples/split_delay_arp.pulse)

Concepts:

- `transform split` makes named branches.
- each branch can be processed differently.
- multiple branches can merge back into the same output.

What to try:

1. Move the split boundaries.
2. Increase the low-note delay.
3. Route the high band into another processor instead of straight out.

Key idea:

This is the core modular routing model: split, process in parallel, merge.
