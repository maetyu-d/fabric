# Fabric

Fabric is a small, human-readable patch language for building modular MIDI music systems inside JUCE-based MIDI Effect plugins. The project now has four plugin variants, Fabric Generate, Fabric Process, Fabric Capture, and Fabric Hub (all with AU, VST3, and standalone targets - only AU is tested). 

Fabric Generate is primarily for the generation of MIDI data from scratch, Fabric Process is primarily for transforming incoming MIDI, Fabric Capture enables incoming MIDI to be recorded, exported, and played back, and Fabric Hub enables MIDI to be sent and received over OSC (from within a MIDI FX plugin). The plugins can be used singularly or in combination, including in combination with any other plugins that send or receive MIDI.

Possibilities include pattern generation, extracting motion and structure from MIDI input, building modulation and control shapes, and connecting together analysers, generators, and processors. The aim is to make diverse, modular, (mostly) MIDI musical systems as easily (human) readable as possible. All plugins feature a handy IO visuualiser too (press Tab to access).

<img width="1160" height="863" alt="Screenshot 2026-04-17 at 13 26 32" src="https://github.com/user-attachments/assets/0f736031-5435-4e3f-b269-dbf3a1bcddcd" />
<img width="1160" height="863" alt="Screenshot 2026-04-17 at 13 27 00" src="https://github.com/user-attachments/assets/02686c2d-8a9b-4dfa-ba2b-c9e62a016e63" />
<img width="1160" height="863" alt="Screenshot 2026-04-17 at 13 27 35" src="https://github.com/user-attachments/assets/e63282de-c8d3-4509-805f-18d46369772f" />


Fabric is a small, human-readable patch language for building modular MIDI music systems inside JUCE-based MIDI effect plugins. The project now builds two plugin variants, `Fabric Generate` and `Fabric Process` (with VST3, AU, and standalone targets). It is primarily for:

- generating MIDI
- transforming MIDI
- extracting motion and structure from MIDI input
- building modulation and control shapes
- connecting together analysers, generators, and processors

The aim is to make diverse, modular, (mostly) MIDI musical systems as easily (human) readable as possible. There's a handy visuualiser too (press Tab to access).



## Design Goals

Fabric is designed around six goals:

1. Be readable by musicians, not just programmers.
2. Run safely inside a real-time JUCE plugin.
3. Support both practical MIDI tools and experimental composition systems.
4. Treat rhythm, pitch, gates, and modulation as first-class signals.
5. Make modular routing explicit and easy to follow.
6. Stay small enough to implement without becoming a full general-purpose language.

## Surface Syntax

Fabric uses short, readable forms for the most common things.

You write:

```pulse
patch first_steps

key D dorian

midi in keys
  channel 1
end

clock metro
  every 1/16
end

pattern riff
  notes D3 F3 A3 C4
  order up_down
end

notes player
  key D dorian
  gate 60%
end

midi out out
end

metro -> riff.trigger
riff -> player
player -> out
end
```

Core forms:

- `midi in <name>` and `midi out <name>`
- `clock`, `pattern`, `random`, `chance`, `table`, `markov`, `tree`
- `field`, `formula`, `moment`, `growth`, `swarm`, `collapse`, `section`, `phrase`, `progression`
- `motion`
- `stages`, `lists`, `modulator`
- `quantize`, `sieve`, `split`, `delay`, `loop`, `bounce`, `arp`, `warp`, `filter`, `bits`
- `groove`, `retrig`, `length`
- `smear`, `cutup`, `slicer`, `pool`
- `subpatch`, `module`, `use`, `probability`
- `notes <name>`
- `key ...`
- `a -> b`

Property language is also kept short in common places:

- inside `notes`:
  - `quantize D dorian` instead of `scale D dorian`
- inside `random`:
  - `held notes` instead of `from held notes`
- inside `stage` lines:
  - `ch` instead of `channel`
  - `to` instead of `level`
  - `for` or `in` instead of `time`

For example:

```pulse
modulator mod1
  channels 2
  mode loop
  stage 1 ch 1 to 0.15 for 70ms curve linear
  stage 2 ch 1 to 0.85 for 110ms curve smooth
end
```

The same idea extends into the more advanced modules too:

```pulse
growth crystal
  root C3
  family perfect weight 1.4 ratios 3/2 4/3
  family color weight 0.9 ratios 5/4 7/4 9/8
  target tonic
  add when stable
  prune when unstable
  max notes 8
end
```

```pulse
collapse engine1
  ruleset stable avoid tonic no repeated intervals
  on collapse cycle broken release stable
  recover to tonic
  follow phrase
end
```

```pulse
notes chords
  chord scale7
  movement nearest
  arrive on 4
end
```

```pulse
probability drifting
  distribution brownian
  burst 0.72
end

random line1
  using drifting
  notes D3 F3 A3 C4 E4
end

field cloud1
  using drifting
  center D3
  spread 7
end
```

```pulse
module phrase key_root key_mode gate_pct
  in midi input
  in trigger tick
  out midi output

  quantize q
    scale $key_root $key_mode
  end

  arp arp1
    gate $gate_pct
  end

  input -> q
  q -> arp1
  tick -> arp1.trigger
  arp1 -> output
end

use phrase left key_root=D key_mode=dorian gate_pct=68%
use phrase right key_root=C key_mode=minor gate_pct=54%
```

```pulse
slicer cuts
  capture 1/4
  slices 8
  drift start 4ms
  drift size 3ms
  reverse 25%
end

pool seqcuts
  capture 1/4
  slices 8
  steps 0 0 3 1 4 2 6 5
  reverse on
end

retrig hats
  count 3
  spacing 22ms -> 8ms
  velocity 94 -> 44
  shape accelerate
end

groove shuffle
  offsets -5ms 2ms -1ms 4ms
  chance 85%
end

length tight
  multiply 0.35
  clamp 12ms..70ms
end
```


## What Fabric Is In More Detail

Fabric is a graph language. A Fabric patch is a set of named modules connected by explicit signal routes. Modules fall into a small number of families:

- `input`
- `analyze`
- `generate`
- `shape`
- `transform`
- `memory`
- `project`
- `output`

Each module:

- has a kind
- has a name
- declares properties
- exposes typed inputs and outputs

Connections are written explicitly:

```pulse
keys -> motion
motion.pulse1 -> bass.trigger
bass -> out
```

## A First Example

```pulse
patch live_memory_machine

scale D minor
tempo 120

midi in keys
  channel 1
end

motion motion1
  channels 8
  space 0.4
  clocked on
end

quantize in_key
  scale D minor
end

split bands
  by note
  low below C3
  mid C3..B4
  high above B4
end

delay low_late
  time 40ms
end

smear afterimage
  keep 3 notes
  weights 0.60 0.25 0.15
  drift weights 0.05
end

midi out out
end

keys -> motion1
keys -> in_key
in_key -> bands
bands.low -> low_late
bands.mid -> afterimage
low_late -> out
afterimage -> out
end
```

That patch says:

- take live MIDI input
- derive motion information from it
- quantise it to a scale
- split it into note bands
- delay the low notes
- smear the middle notes through pitch memory
- send the result out

## Reuse And Composition

Fabric now supports three different levels of reuse:

- `probability`
  - define one stochastic profile and reuse it with `using ...`
- `subpatch`
  - define a local nested patch with typed `in` and `out` ports
- `module` / `use`
  - define a reusable local patch fragment and instantiate it many times with arguments

These let you write larger pieces without flattening everything into one long top-level patch.

## Micro-Scale Tools

Fabric can now cover short-loop and micro-sampling (if connected to a sampler plugin) style work too.

- `groove`
  - repeating microtiming offsets
- `retrig`
  - short repeat bursts with spacing and velocity curves
- `length`
  - note-length crushing and reshaping
- `slicer`
  - capture a short note window and replay slices
- `pool`
  - sequence captured slices explicitly with `steps ...`

Useful example patches:

- [micro_house_tools.pulse](/Users/md/Downloads/plugin%20language/examples/micro_house_tools.pulse)
- [micro_slice_pool.pulse](/Users/md/Downloads/plugin%20language/examples/micro_slice_pool.pulse)
- [shared_probability.pulse](/Users/md/Downloads/plugin%20language/examples/shared_probability.pulse)

## Signal Types

Fabric has five core signal types.

### `midi`

Discrete MIDI events:

- note on
- note off
- cc
- pitch bend
- aftertouch
- program change

### `trigger`

A short event used to advance, fire, or sample something.

Examples:

- clock ticks
- note-derived pulses
- threshold crossings
- random impulses

### `gate`

A held on/off state with duration.

Examples:

- note held
- channel active
- odd/even activity gate
- pattern open/closed

### `value`

A continuous or stepped numeric signal.

Examples:

- LFO output
- envelope level
- motion speed
- stage interpolation
- density

### `pitch`

A musical pitch representation before final MIDI note output.

This is useful when a process produces pitch-like motion that should later be:

- quantised
- harmonised
- projected into a scale
- converted to MIDI note numbers

Keeping `pitch` separate from raw MIDI notes gives the language room for more experimental ideas without making everything obscure.

## Module Families

Each module belongs to a family with a musical purpose.

## 1. `input`

Input modules receive external information.

Examples:

- live MIDI from the host
- note input
- CC streams
- trigger input from another process

Example:

```pulse
midi in keys
  channel any
end
```

## 2. `analyze`

Analyze modules derive structure from incoming material.

This is where example 2 belongs.

They do not just pass MIDI through. They extract motion, activity, and event structure from MIDI behavior.

Example:

```pulse
motion tracker
  channels 8
  space 0.35
  clocked on
end
```

Possible outputs:

- `tracker.pulse1`
- `tracker.pulse2`
- `tracker.speed`
- `tracker.even`
- `tracker.odd`

Useful analyze kinds:

- `motion`
- `density`
- `zones`
- `crossings`
- `activity`
- `intervals`
- `direction`

## 3. `generate`

Generate modules create new musical material.

These are not necessarily step sequencers only. They can be clocks, patterns, agents, or rule systems.

Examples:

- `clock`
- `pattern`
- `fibonacci`
- `growth`
- `swarm`
- `collapse`
- `random`

Simple example:

```pulse
clock metro
  every 1/16
end
```

Algorithmic example:

```pulse
fibonacci fib1
  length 8
  map 0 2 3 5 7
  use as rhythm
end
```

## 4. `shape`

Shape modules create or process staged, continuous, or interpolated control structures.

This family is central to your examples.

It covers:

- multi-stage envelopes
- complex LFOs
- voltage-style processors
- Buchla MARF-style list traversal
- interpolation between stored points

Example:

```pulse
stages mod_a
  mode loop
  stages 13

  stage 1 level 0.10 time 40ms curve exp
  stage 2 level 0.75 time 120ms curve smooth
  stage 3 level 0.20 time 2s curve log
end
```

MARF-like example:

```pulse
lists marf1
  pitch C3 E3 G3 Bb3
  time 80ms 140ms 220ms 500ms
  gate 30% 60% 90% 40%

  advance pitch on note
  advance time on random
  advance gate on threshold 0.7

  interpolate pitch on
  interpolate time on
end
```

## 5. `transform`

Transform modules modify incoming material.

This is the practical workhorse family.

Examples:

- `quantize`
- `transpose`
- `filter`
- `split`
- `delay`
- `bounce`
- `loop`
- `bits`
- `velocity`
- `length`
- `humanize`
- `arp`

Examples:

```pulse
quantize in_key
  scale C minor
end
```

```pulse
filter high_notes
  type note
  pass above C4
end
```

```pulse
bits crush_velocity
  target velocity
  and 11110000b
end
```

## 6. `memory`

Memory modules work with history, residue, fragment stores, and temporal blending.

This is where the language becomes distinctive.

Examples:

- temporal smear
- cut-up fragment recombination
- history-weighted pitch blending
- phrase residue
- continuity-based rearrangement

Example:

```pulse
smear bergson
  keep 3 notes
  weights 0.60 0.25 0.15
  drift weights 0.05
end
```

Cut-up example:

```pulse
cutup burroughs
  slice 1/16..1 beat
  tag automatically
  favor harmonic
  keep continuity 0.35
end
```

## 7. `project`

Project modules map abstract material into final usable musical form.

This is where `value` or `pitch` streams become performable notes.

Examples:

- project values into a scale
- map warped pitch space back to notes
- convert continuous pitch into MIDI output

Example:

```pulse
notes notes1
  scale D dorian
  range C2..C6
end
```

## 8. `output`

Output modules emit final results.

Example:

```pulse
midi out out
end
```

Versions 0.1 and 0.2 only really support `output midi`, but debug or monitor outputs could be added later.

## Syntax

To be added.

## Patch Structure

```text
patch <name>
  <global-setting>*
  <module>*
  <connection>*
end
```

## Module Structure

```text
<family> <kind> <name>
  <property>*
end
```

Examples:

```pulse
clock metro
  every 1/16
end

delay late
  time 80ms
end
```

## Connections

```text
<from> -> <to>
<from>.<port> -> <to>
<from> -> <to>.<port>
<from>.<port> -> <to>.<port>
```

Examples:

```pulse
keys -> split1
split1.high -> arp1
motion1.speed -> mod_a.rate
metro -> seq1.trigger
```

## Properties

Properties are plain key-value lines:

```pulse
scale D minor
gate 70%
amount +12
time 240ms
```

## Lists

Lists are simple and space-separated:

```pulse
notes C3 E3 G3 Bb3
weights 0.60 0.25 0.15
ratios 3/2 5/4 7/4
series 1 1 2 3 5 8 13
```

## Comments

Use `#`:

```pulse
# blur the middle register through note memory
smear bergson
  keep 3 notes
end
```

## Time

Fabric prefers musical time, but allows milliseconds where needed.

Supported forms:

- `1/4`
- `1/8`
- `1/16`
- `2 beats`
- `4 bars`
- `20ms`
- `3s`
- `2m`

## Core Use Cases From Your Examples

These examples give an idea of what the language is optomised for:

## 1. Multi-Channel Stage Modulation

A four-channel staged slope engine that can behave like:

- complex envelope
- flexible LFO
- step sequence

Example:

```pulse
stages mod4
  channels 4
  stages 13
  overlap on
  run free

  stage 1 level 0.1 time 20ms curve exp
  stage 2 level 0.8 time 200ms curve smooth
  stage 3 level 0.3 time 2s curve log
end
```

Thus, the language needs:

- channels
- per-stage level, time, and curve
- trigger or free-run mode
- live modulation of stage properties

## 2. Motion Extraction From MIDI

Derive pulses, speed events, and activity gates from incoming MIDI motion.

Example:

```pulse
midi in performer
  channel 1
end

motion motion1
  channels 8
  space 0.4
  clocked on
end

performer -> motion1
```

This implies:

- MIDI analysis as a first-class module family
- multiple typed outputs
- optional synchronization input

## 3. Quantized Harmonic Reinterpretation

Take incoming material and force it into harmonic structure.

Example:

```pulse
quantize lock
  scale A minor
end
```

This needs to work for:

- MIDI note streams
- unquantized sequences
- control/value streams that are being used as pitch

## 4. Hybrid Sequencer Envelope Processor

A 16-stage system that can operate as sequence, envelope, or non-linear voltage processor.

This belongs naturally in `shape`.

## 5. Fibonacci and User-Defined Pattern Generation

Example:

```pulse
fibonacci fib1
  length 8
  use as rhythm
end
```

or:

```pulse
pattern custom
  series 1 1 2 3 5 8 13
  loop on
end
```

Algorithmic pattern modules try be plainly readable and friendly, not maths-heavy.

## 6. MIDI Highpass and Lowpass Filters

These are musical filters for MIDI data:

Examples:

```pulse
filter low_notes
  type note
  pass below C4
end
```

```pulse
filter strong_hits
  type velocity
  pass above 90
end
```

## 7. Bitwise MIDI Operations

Fabric allows low-level operations, but tries to keep them readable and scoped.

Example:

```pulse
bits xor_vel
  target velocity
  xor 00001111b
end
```

Versions 0.1 and 0.2 apply these only to well-defined fields:

- note
- velocity
- cc value
- channel

(not arbitrary raw byte hacking)

## 8. Split MIDI Into Bands and Separately Delay Each Band

Example:

```pulse
split bands
  by note
  low below C3
  mid C3..B4
  high above B4
end

delay low_late
  time 30ms
end

delay mid_late
  time 80ms
end

delay high_late
  time 140ms
end
```

This requires:

- named outputs
- parallel routing
- branch merging
- MIDI event delay with proper timestamp handling

## 9. Bouncing Ball MIDI

Example:

```pulse
bounce ball
  count 8
  spacing 240ms -> 20ms
  velocity 110 -> 40
end
```

This needs a temporal gesture processor:

- repeated events
- changing spacing
- changing dynamics
- optional pitch drift

## 10. MIDI Looping

Example:

```pulse
loop phrase1
  capture 1 bar
  playback on
  overdub off
  quantize to 1/16
end
```

This requires the language to distinguish:

- phrase loops
- note loops
- event loops
- overdub loops

## 11. Temporal Smearing

Example:

```pulse
smear bergson
  keep 3 notes
  weights 0.60 0.25 0.15
  drift weights 0.05
end
```

This requires:

- history buffers
- weighted blending
- parameter drift
- conversion back to discrete pitch when needed

## 12. Topological Pitch Space

Not yet fully implemented.

Concept:

- represent pitch in a warped internal space
- transform it by folds and wormholes
- map it back to notes later

This will probably belong in `project` or in advanced `memory/project` modules rather than the core language syntax.

## 13. Harmonic Crystal Growth

After James Tenney.

Example:

```pulse
growth crystal
  root C2
  ratios 3/2 5/4 7/4
  add when stable
  prune when unstable
  map growth to density
end
```

This introduces self-organising harmony as a generator family.

## 14. Multi-Agent MIDI Swarm

Example:

```pulse
swarm voices
  agents 6

  agent 1 type anchor
  agent 2 type follower
  agent 3 type follower
  agent 4 type rebel
  agent 5 type rebel
  agent 6 type follower

  center D
  cluster 0.7
  negotiate rhythm
end
```

This is part of the advanced generative layer.

## 15. Constraint Collapse Engine

Example:

```pulse
collapse engine1
  rules
    no repeated intervals
    max step 2
    avoid tonic
  end

  on collapse mutate 2 rules
end
```

This leads Fabric to allow a readable rule syntax for some advanced generators.

## 16. Endless Cut-Up Sequencer

Example:

```pulse
cutup burroughs
  slice 1/16..1 beat
  tag automatically
  favor harmonic
  keep continuity 0.35
end
```

This adds:

- fragment storage
- derived tags
- probabilistic recombination
- continuity constraints

## 17. Voltage-style List Engine

Example:

```pulse
lists marf1
  pitch C3 E3 G3 Bb3
  time 80ms 140ms 220ms 500ms
  gate 30% 60% 90% 40%

  advance pitch on note
  advance time on threshold 0.6
  advance gate on random

  interpolate pitch on
  interpolate time on
end
```

Requires `shape` to be a top-level family.


## V1 Signal Types

- `midi`
- `trigger`
- `gate`
- `value`
- `pitch`

## V1 Module Families

- `input`
- `analyze`
- `generate`
- `shape`
- `transform`
- `project`
- `output`

`memory` can begin with a minimal `smear` or `loop` module, but it does not need its full future range on day one.

## V1 Module Kinds

### Input

- `midi`

### Analyze

- `motion`
- `density`
- `activity`

### Generate

- `clock`
- `pattern`
- `fibonacci`
- `random`

### Shape

- `stages`
- `lists`
- `lfo`

### Transform

- `quantize`
- `transpose`
- `filter`
- `split`
- `delay`
- `bounce`
- `loop`
- `bits`
- `velocity`
- `length`
- `humanize`
- `arp`

### Project

- `to_notes`
- `to_values`

### Output

- `midi`

That is already a powerful system.


## Grammar

A simple block grammar:

```text
patch <name>
  <global>*
  <module>*
  <connection>*
end

<module> ::= <family> <kind> <name>
             <property>*
             end

<connection> ::= connect <source> -> <target>
```

Where:

- `<family>` is one of `input`, `analyze`, `generate`, `shape`, `transform`, `memory`, `project`, `output`
- `<kind>` is a built-in module kind
- `<name>` is a unique module name

## Type Rules

Connections are checked at compile time.

Examples:

- `clock` outputs `trigger`
- `stages` outputs `value`
- `quantize` may accept `pitch` or `midi`
- `split` accepts `midi`
- `to_notes` accepts `pitch` or `value` and outputs `midi`
- `motion.speed` may output `value`
- `motion.even` may output `gate`

Bad connections fail with readable errors:

```text
Cannot connect `metro` to `transpose1`.
`metro` outputs trigger, but `transpose` expects midi or pitch.
```

## Compile Model

Fabric compiles before playback and does not parse or interpret script text inside the audio callback.

The processing model is:

1. Parse source text into an AST.
2. Validate module names, properties, and references.
3. Resolve signal types and ports.
4. Build a fixed directed graph.
5. Reject illegal cycles in v1.
6. Preallocate runtime nodes and event buffers.
7. Run the graph each `processBlock`.

## JUCE Runtime Model

Inside a JUCE MIDI effect plugin, the architecture is something like:

- `PulseLexer`
- `PulseParser`
- `PulseAst`
- `FabricCompiler`
- `CompiledPatch`
- `RuntimeNode`
- `EventBuffer`
- `MidiScheduler`

## Processing Per Block

For each `processBlock`:

1. Read host tempo if enabled, otherwise use the patch tempo.
2. Feed incoming MIDI into `midi in` nodes.
3. Advance clocks and timed shapes.
4. Run graph nodes in topological order.
5. Route typed events between nodes.
6. Schedule note-offs and delayed MIDI correctly.
7. Emit the final `juce::MidiBuffer`.

## Real-Time Safety Rules

Fabric is strict in terms of:

- no dynamic allocation in `processBlock`
- no file IO in `processBlock`
- no runtime reparsing in `processBlock`
- bounded per-block work
- preallocated buffers for events and node state

This matters because some modules, like loops or smears, are likely to get heavy if not constrained.

## Errors

Errors try to read like helpful/constructive editor guidance.

Examples:

```text
Unknown module `bergon`.
Did you mean `bergson`?
```

```text
Line 24: `weights 0.6 0.25` is incomplete.
`smear` expects 3 weights because `keep 3 notes` was set.
```

```text
Cannot connect `tracker.speed` to `split1`.
`tracker.speed` outputs value, but `split` expects midi.
```

# Tutorials

It's recommended to start with the guided lesson set in [tutorials/README.md](/Users/md/Downloads/plugin%20language/tutorials/README.md). It includes 15 progressive tutorials, from a first clocked pattern to growth, collapse, and the complex modulator.
