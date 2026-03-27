# 25 Chance Operations

`chance` is Fabric's direct indeterminacy module. It gives you coin tosses, dice throws, and I Ching style choices as real patchable outputs.

```pulse
patch chance_basics

clock metro
  every 1/8
end

chance coin_flip
  coin C3 G3
  seed 42
end

chance dice_throw
  dice D3 F3 A3 C4 E4
  seed 7
end

chance oracle
  i_ching C3 Eb3 G3 Bb3 D4 F4 A4
  seed 64
end

notes notes1
  quantize D dorian
  gate 60%
  velocity 98
end

midi out out
end

metro -> oracle.trigger
oracle.pitch -> notes1
notes1 -> out
end
```

Useful ports:

- `oracle.out` gives the chosen value
- `oracle.pitch` gives the chosen pitch
- `oracle.trigger` fires when a choice is made
- `oracle.index` gives the face or hexagram index

Try swapping `coin`, `dice`, and `i_ching` to hear different kinds of musical indeterminacy.
