# Research report

rows=99950  out-of-sample rows=69965  horizon=50 samples  folds=5

| model | IC | null IC range (shifted labels) | p-value | 95% block-bootstrap CI | hit rate (up-rate) | OOS R2 vs zero |
|---|---|---|---|---|---|---|
| ridge | +0.2512 | [-0.0657, +0.0542] | 0.048 | [+0.2120, +0.3088] | 0.661 (0.525) | -0.0283 |
| sign(imb1) | +0.1449 | [-0.0289, +0.0460] | 0.048 | [+0.0909, +0.2066] | 0.595 (0.525) |  |

p-value = (1 + #null runs with |IC| >= |real IC|) / (1 + 20); the smallest possible is 0.048. The null uses 20 circular label shifts, which respects long-lived autocorrelation that the bootstrap interval can understate.

Caveat: this measures statistical predictability of the mid move, not tradable profit (no spread cost, queue position, latency or fees). Synthetic data proves the harness, not a market edge.
