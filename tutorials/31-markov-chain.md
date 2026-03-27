# 31 Markov Chain

Use `markov` when each state should suggest the next one.

```pulse
markov chain1
  start C3
  state C3 -> Eb3 0.55 G3 0.45
  state Eb3 -> G3 0.60 Bb3 0.40
end
```

Each trigger emits the current state, then walks to the next one by weighted transition.

Try:
- make one transition dominant
- add a returning state to close the loop
