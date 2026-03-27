# 35 Shared Probability

Use `probability` when you want one stochastic definition reused across several modules.

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

This keeps related generators feeling like they belong to the same musical world.

Try:
- swap `brownian` for `gaussian`
- add `lambda 2.8` and reuse it in `growth`
- define two probability objects and compare the results
