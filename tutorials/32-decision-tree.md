# 32 Decision Tree

Use `tree` when you want branching choices that resolve to leaves.

```pulse
tree oracle
  root root
  node root -> branch_low 0.45 branch_high 0.55
  node branch_low -> C3 0.55 Eb3 0.45
  node branch_high -> G3 0.50 Bb3 0.50
end
```

Each trigger walks the tree until it reaches a leaf, then outputs that result.

Try:
- add another branch layer
- use note names in one branch and numbers in another
