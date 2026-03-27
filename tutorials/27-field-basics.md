# 27 Field Basics

`field` generates a drifting cloud of pitches around a center point.

```pulse
patch field_basics

clock metro
  every 1/16
end

field cloud1
  center D3
  spread 9
  density 0.72
  drift 0.6
  distribution gaussian
  emit 3
  register mid
  seed 33
end

notes notes1
  quantize D dorian
  gate 52%
  velocity 92
end

midi out out
end

metro -> cloud1.trigger
cloud1 -> notes1
notes1 -> out
end
```

Use `distribution`, `emit`, and `register` to move between sparse lines and denser note masses.
