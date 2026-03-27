# 22 CC Bit Glitch

Goal: corrupt a specific controller lane without touching the rest of the MIDI stream.

Patch: [examples/cc_glitch.pulse](/Users/md/Downloads/plugin%20language/examples/cc_glitch.pulse)

Concepts:

- `transform bits` can target controller data directly.
- `status cc`, `cc`, `channel`, and exclusion filters let you aim precisely.
- this is great for modwheel, macros, and live controller manipulation.

Key idea:

Fabric can work as a controller-mangling tool, not just a note processor.
