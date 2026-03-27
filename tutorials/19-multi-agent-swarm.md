# 19 Multi-Agent Swarm

Goal: let several simple musical agents negotiate the output together.

Patch: [examples/swarm_machine.pulse](/Users/md/Downloads/plugin%20language/examples/swarm_machine.pulse)

Concepts:

- `generate swarm` creates multiple internal agents.
- follower, rebel, and anchor roles pull the melody in different directions.
- `distribution brownian` makes voice choice drift over time instead of jumping evenly.
- the merged result can then be projected into playable MIDI.

Key idea:

Swarm patches are about interaction, not linear sequencing.
