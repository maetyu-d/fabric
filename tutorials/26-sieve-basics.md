# 26 Sieve Basics

`sieve` uses modular arithmetic to decide which pitches or pulses are allowed through.

```pulse
patch sieve_basics

clock metro
  every 1/16
end

pattern seq1
  notes C3 D3 Eb3 F3 F#3 G3 A3 Bb3
  order up_down
end

sieve mask1
  mod 12 keep 0 1 4 7
end

sieve rhythm1
  mod 8 keep 0 3 5
end

notes notes1
  quantize C major
  gate 60%
  velocity 100
end

midi out out
end

metro -> rhythm1.trigger
seq1 -> mask1
mask1 -> notes1
rhythm1.trigger -> notes1.gate
notes1 -> out
end
```

Use one sieve for pitch and another for rhythm. That makes it easy to build structured but non-scale-based patterns.
