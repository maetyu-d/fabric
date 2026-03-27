# 33 Subpatch Basics

Use `subpatch` to group a small local patch behind typed ports.

```pulse
subpatch phrase
  in midi input
  in trigger tick
  out midi output

  quantize q
    scale D dorian
  end

  arp arp1
    gate 68%
  end

  input -> q
  q -> arp1
  tick -> arp1.trigger
  arp1 -> output
end
```

Outside the block, connect to `phrase.input`, `phrase.tick`, and `phrase.output`.

Try:
- add another input port for `gate` or `value`
- swap the inner modules without changing the outside wiring
