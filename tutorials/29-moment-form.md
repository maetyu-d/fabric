# 29 Moment Form

`moment` moves between named musical states instead of developing one line continuously.

```pulse
patch moment_form

clock metro
  every 1/16
end

moment form1
  start still
  jump weighted
  transition carry
  moment still 4 C3 E3 G3
  moment burst 2 D4 F4 A4 C5
  moment echo 3 Bb2 D3 F3
  chance still -> burst 0.7
  chance burst -> echo 0.6
  chance echo -> still 0.8
end

notes notes1
  quantize C major
  gate 60%
  velocity 98
end

midi out out
end

metro -> form1.trigger
form1 -> notes1
notes1 -> out
end
```

Think of each `moment` as a self-contained musical state, then use weighted `chance` lines to decide where the form jumps next.
