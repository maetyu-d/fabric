# 24 Equation Basics

`equation` lets you write a small expression and turn it into pitch, value, or MIDI changes.

```pulse
patch equation_melody

clock metro
  every 1/16
end

equation curve1
  pitch = 60 + sin(t * 2.4) * 7 + cos(t * 0.5) * 4
end

notes notes1
  quantize D dorian
  gate 58%
  velocity 96
end

midi out out
end

metro -> curve1.trigger
curve1.pitch -> notes1
notes1 -> out
end
```

Try changing the constants in the `sin` and `cos` terms to hear how the melody bends into new shapes.
