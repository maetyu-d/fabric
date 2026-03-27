# 30 Choice Table

Use `table` when you want explicit weighted rule objects.

```pulse
table choices
  rule C3 weight 3.0
  rule Eb3 weight 1.5
  rule G3 weight 2.5
  rule Bb3 weight 1.0
end
```

Each trigger picks one rule by weight.

Try:
- increase one weight a lot and listen to the bias
- swap note choices for values and feed another module
