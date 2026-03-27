# 34 Reusable Modules

Use `module` and `use` when you want to define a local patch once and instantiate it more than once.

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

Each `use` creates a new instance with its own local internals and typed ports.
